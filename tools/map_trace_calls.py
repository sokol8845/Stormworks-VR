"""Map observed return RVAs to x64 PE exception ranges, without changing the game.

Exception ranges are unwind entries, not proof of complete logical functions.
Old traces without an executable hash remain explicitly unverified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


class PeImage:
    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('Not a PE image')
        pe = self.unpack('I', 0x3c)[0]
        if data[pe:pe+4] != b'PE\0\0':
            raise ValueError('Invalid PE signature')
        machine, count = self.unpack('HH', pe+4)
        optional_size = self.unpack('H', pe+20)[0]
        optional = pe+24
        if machine != 0x8664 or self.unpack('H', optional)[0] != 0x20b:
            raise ValueError('Expected x64 PE32+')
        self.timestamp = self.unpack('I', pe+8)[0]
        self.base = self.unpack('Q', optional+24)[0]
        self.size = self.unpack('I', optional+56)[0]
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.sections = []
        for i in range(count):
            start = optional+optional_size+40*i
            name = data[start:start+8].rstrip(b'\0').decode('ascii', errors='replace')
            virtual_size, rva, raw_size, raw = self.unpack('IIII', start+8)
            flags = self.unpack('I', start+36)[0]
            self.sections.append(dict(name=name, rva=rva, size=max(virtual_size, raw_size),
                                      raw_size=raw_size, raw=raw, executable=bool(flags & 0x20000000)))
        directory_count = self.unpack('I', optional+108)[0]
        self.ranges = []
        if directory_count > 3:
            rva, length = self.unpack('II', optional+112+3*8)
            if rva and length:
                if length % 12:
                    raise ValueError('Invalid exception directory length')
                table = self.offset(rva, length)
                for at in range(table, table+length, 12):
                    begin, end, unwind = self.unpack('III', at)
                    if begin < end <= self.size:
                        self.ranges.append((begin, end, unwind))

    def unpack(self, fmt, offset):
        if offset < 0 or offset+struct.calcsize('<'+fmt) > len(self.data):
            raise ValueError('Truncated PE field')
        return struct.unpack_from('<'+fmt, self.data, offset)

    def section(self, rva):
        if not 0 <= rva < self.size:
            raise ValueError('RVA outside image')
        return next((s for s in self.sections if s['rva'] <= rva < s['rva']+s['size']), None)

    def offset(self, rva, length=1):
        section = self.section(rva)
        if section is None or length < 0 or rva-section['rva']+length > section['raw_size']:
            raise ValueError('RVA is not backed by file bytes')
        at = section['raw']+rva-section['rva']
        if at+length > len(self.data):
            raise ValueError('Truncated PE section')
        return at

    def locate_return(self, rva):
        # A return address can be exactly at a range end; the call is before it.
        section = self.section(rva-1)
        if section is None or not section['executable']:
            raise ValueError('Return address is not in executable code')
        ranges = [dict(begin_rva=hex(begin), end_rva=hex(end),
                       begin_va=hex(self.base+begin), unwind_rva=hex(unwind))
                  for begin, end, unwind in self.ranges if begin <= rva-1 < end]
        start = max(section['rva'], rva-16)
        at = self.offset(start, rva-start)
        return dict(return_rva=hex(rva), return_va=hex(self.base+rva), section=section['name'],
                    preceding_bytes=self.data[at:at+rva-start].hex(), unwind_ranges=ranges)


def map_calls(summary, image):
    expected = summary.get('metadata', {}).get('exe_sha256')
    if expected and expected != image.sha256:
        raise ValueError('Executable SHA-256 differs from capture; refusing address mapping')
    results = []
    for call in summary['matrix_calls']:
        relative = call.get('caller_offset_from_exe_base')
        if relative is None:
            continue
        entry = dict(uniform=call['name'], count=call['count'], program=call['program'])
        try:
            entry.update(image.locate_return(int(relative, 16)))
        except ValueError as error:
            entry.update(return_rva=relative, error=str(error))
        results.append(entry)
    return dict(sha256=image.sha256, identity_verified=bool(expected),
                image_base=hex(image.base), image_size=hex(image.size), timestamp=image.timestamp,
                calls=results, limitations=[
                    'Unwind entries can cover fragments or chained portions of a function.',
                    'Preceding bytes are evidence for disassembly, not an instruction decoder.',
                    'No camera hook is approved by this mapping.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('summary', type=Path)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = map_calls(json.loads(args.summary.read_text(encoding='utf-8-sig')),
                       PeImage(args.exe.read_bytes()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('calls', 'limitations')}))
