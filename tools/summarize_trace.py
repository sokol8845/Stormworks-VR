"""Summarize observational OpenGL traces; pass boundaries are candidates, not proof."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path


def summarize(path, capture_epoch=None):
    frames = defaultdict(lambda: {'draws': 0, 'segments': []})
    uniform_names = {}
    matrices = Counter()
    events = Counter()
    ignored_matrices = 0
    metadata = {}
    last = {}
    epoch = 0
    with path.open(encoding='utf-8') as stream:
        header = next(stream).rstrip().split('\t')
        if header[:4] != ['frame', 'thread', 'context', 'event']:
            raise ValueError('Not a Stormworks VR GL trace')
        for line in stream:
            if line.startswith('#'):
                key, value = line.strip().split('\t', 1)
                metadata[key[1:]] = value
                if key == '#capture_epoch':
                    epoch = int(value)
                continue
            row = next(csv.reader([line], delimiter='\t'))
            if len(row) < 10:
                raise ValueError('Incomplete trace row')
            frame, thread, context, event, program, fbo, a, b, c, caller = row[:10]
            key = (context, program, a)
            if event == 'uniform' and len(row) > 10 and int(b) >= 0:
                uniform_names[(context, a, b)] = row[10]
            if capture_epoch is not None and epoch != capture_epoch:
                continue
            events[event] += 1
            if event == 'matrix4':
                if int(a) < 0:
                    ignored_matrices += 1
                else:
                    matrices[(context, program, a, caller)] += 1
            if event.startswith('draw_'):
                f = frames[frame]
                f['draws'] += 1
                # Keep threads and contexts separate; a framebuffer ID alone is not global.
                stream_key = (frame, thread, context)
                state = (program, fbo)
                if last.get(stream_key) != state:
                    f['segments'].append({'thread': thread, 'context': context,
                                          'program': int(program), 'fbo': int(fbo)})
                    last[stream_key] = state
    matrix_calls = [{'context': ctx, 'program': int(program), 'location': int(loc),
                     'name': uniform_names.get((ctx, program, loc)), 'caller': caller, 'count': count}
                    for (ctx, program, loc, caller), count in matrices.most_common()]
    limit = int(metadata.get('event_limit', 0))
    base = int(metadata['exe_base'], 16) if 'exe_base' in metadata else None
    for call in matrix_calls:
        address = int(call['caller'], 16)
        # A relative coordinate is only a candidate until checked against PE SizeOfImage.
        call['caller_offset_from_exe_base'] = hex(address-base) if base is not None else None
    return {'source': str(path), 'selected_epoch': capture_epoch, 'metadata': metadata, 'event_counts': dict(events),
            'ignored_matrix_calls': ignored_matrices,
            'event_limit_reached': bool(limit and sum(events.values()) >= limit),
            'frames': dict(frames), 'matrix_calls': matrix_calls,
            'limitations': ['Segments are state transitions, not identified game rendering passes.',
                           'Only intercepted entry points are counted; this is not a complete GL command stream.',
                           'Uniform names can be unavailable or reused after program relinking/deletion.',
                           'Trace overhead makes this unsuitable for performance benchmarking.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path, default=Path('artifacts/trace-summary.json'))
    parser.add_argument('--epoch', type=int, help='Analyze one requested capture; default includes all captures')
    args = parser.parse_args()
    report = summarize(args.trace, args.epoch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'events': report['event_counts'], 'draw_frames': len(report['frames']),
                      'matrix_call_sites': len(report['matrix_calls']),
                      'event_limit_reached': report['event_limit_reached']}, indent=2))
