"""Verify native camera-update observations against the exact captured executable."""
import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
from map_trace_calls import PeImage


def verify(path, image, capture_epoch=None):
    metadata = {}
    pending = {}
    callers = Counter()
    objects = set()
    samples = failures = 0
    max_relative_error = 0.0
    errors = []
    epoch = 0
    incomplete = 0
    with path.open(encoding='utf-8') as stream:
        header = next(stream).rstrip().split('\t')
        if header[:4] != ['frame', 'thread', 'context', 'event']:
            raise ValueError('Not a Stormworks VR trace')
        for line in stream:
            if line.startswith('#'):
                key, value = line.rstrip().split('\t', 1)
                metadata[key[1:]] = value
                if key == '#capture_epoch':
                    incomplete += len(pending)
                    pending.clear()
                    epoch = int(value)
                continue
            row = next(csv.reader([line], delimiter='\t'))
            if len(row) < 10:
                raise ValueError('Incomplete trace row')
            kind = row[3]
            if kind not in ('camera_view', 'camera_projection', 'camera_view_projection'):
                continue
            if capture_epoch is not None and epoch != capture_epoch:
                continue
            if len(row) != 26:
                raise ValueError('Incomplete camera matrix')
            values = [float(value) for value in row[10:]]
            if not all(math.isfinite(value) for value in values):
                raise ValueError('Non-finite camera matrix')
            key = (row[1], row[6], row[9])  # thread, camera pointer, caller
            if kind == 'camera_view':
                pending[key] = {'view': values}
            elif kind == 'camera_projection':
                pending.setdefault(key, {})['projection'] = values
            else:
                sample = pending.pop(key, {})
                if not all(name in sample for name in ('view', 'projection')):
                    raise ValueError('VP without preceding view/projection pair')
                v, p = sample['view'], sample['projection']
                product = [sum(p[k*4+r]*v[c*4+k] for k in range(4))
                           for c in range(4) for r in range(4)]
                relative = max(abs(a-b)/(1+abs(b)) for a, b in zip(product, values))
                max_relative_error = max(max_relative_error, relative)
                failures += relative > 1e-4
                samples += 1
                callers[int(row[9], 16)] += 1
                objects.add(row[6])
    if metadata.get('exe_sha256') != image.sha256:
        errors.append('Trace executable SHA-256 does not match supplied image')
    if metadata.get('camera_probe') != 'enabled':
        errors.append('Native camera probe was not enabled')
    if not samples:
        errors.append('No complete native camera samples')
    if failures:
        errors.append(f'{failures} camera samples disagree with projection * view')
    caller_report = []
    base = int(metadata.get('exe_base', '0'), 16)
    for address, count in callers.most_common():
        try:
            mapped = image.locate_return(address-base)
        except ValueError as error:
            errors.append(f'Caller outside validated code: {error}')
            mapped = {'return_rva': hex(address-base)}
        caller_report.append(dict(count=count, **mapped))
    return dict(passed=not errors, selected_epoch=capture_epoch, errors=errors, samples=samples, failures=failures,
                unique_camera_addresses=len(objects), max_relative_error=max_relative_error,
                incomplete_samples=incomplete+len(pending), callers=caller_report, metadata=metadata,
                limitations=['Camera addresses can be reused for stack objects.',
                             'Consistent matrices do not establish pass identity or stereo correctness.',
                             'A capped trace may end partway through one camera sample.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--epoch', type=int, help='Validate one capture rather than all appended captures')
    args = parser.parse_args()
    report = verify(args.trace, PeImage(args.exe.read_bytes()), args.epoch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('callers', 'metadata', 'limitations')}))
    raise SystemExit(0 if report['passed'] else 1)
