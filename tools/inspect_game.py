"""Read-only PE imports and shader inventory. Never patches game files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def pe_imports(data):
    def unpack(fmt, at):
        return struct.unpack_from('<' + fmt, data, at)
    if data[:2] != b'MZ':
        raise ValueError('Not a PE image')
    pe, = unpack('I', 0x3c)
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Bad PE signature')
    machine, sections = unpack('HH', pe+4)
    optional_size, = unpack('H', pe+20)
    optional = pe+24
    magic, = unpack('H', optional)
    if magic not in (0x10b, 0x20b):
        raise ValueError('Unknown optional header')
    is64 = magic == 0x20b
    directory = optional + (112 if is64 else 96)
    imports_rva, imports_size = unpack('II', directory+8)
    section_table = optional+optional_size

    def offset(rva):
        for i in range(sections):
            size, start, raw_size, raw = unpack('IIII', section_table+40*i+8)
            if start <= rva < start+max(size, raw_size):
                if rva-start >= raw_size:
                    raise ValueError('RVA is not backed by file data')
                return raw+rva-start
        raise ValueError(f'Unmapped RVA {rva:x}')

    def string(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii', errors='replace')

    imports = {}
    if not imports_rva:
        return machine, imports
    descriptor = offset(imports_rva)
    for i in range(imports_size // 20):
        original, stamp, forward, name, thunk = unpack('IIIII', descriptor+20*i)
        if not any((original, stamp, forward, name, thunk)):
            break
        functions = []
        at = offset(original or thunk)
        step = 8 if is64 else 4
        while True:
            value, = unpack('Q' if is64 else 'I', at)
            if not value:
                break
            functions.append(f'ordinal:{value & 65535}' if value & (1 << (step*8-1)) else string(value+2))
            at += step
        imports[string(name)] = functions
    return machine, imports


def inspect(directory):
    exe = directory / 'stormworks64.exe'
    data = exe.read_bytes()
    machine, imports = pe_imports(data)
    shader_files = sorted(p for p in directory.rglob('*') if p.is_file() and p.suffix.startswith('.glsl'))
    uniforms = {}
    for path in shader_files:
        text = path.read_text(encoding='utf-8', errors='replace')
        matches = sorted(set(re.findall(r'\buniform\s+\w+\s+(\w+)', text)))
        relevant = [name for name in matches if re.search('view|proj|camera|matrix', name, re.I)]
        if relevant:
            uniforms[str(path.relative_to(directory))] = relevant
    manifest = directory.parent.parent / 'appmanifest_573090.acf'
    match = re.search(r'"buildid"\s+"(\d+)"', manifest.read_text()) if manifest.exists() else None
    return {'executable': str(exe), 'sha256': hashlib.sha256(data).hexdigest(),
            'steam_build_id': match.group(1) if match else None,
            'machine': hex(machine), 'imports': imports,
            'opengl_import_present': any(x.lower() == 'opengl32.dll' for x in imports),
            'shader_file_count': len(shader_files), 'camera_uniform_candidates': uniforms,
            'stereo_rendering': 'not_verified', 'camera_hook': 'not_implemented'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('artifacts/game-report.json'))
    args = parser.parse_args()
    report = inspect(args.game_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('imports','camera_uniform_candidates')}, indent=2))
    print(f'Report: {args.output}')
