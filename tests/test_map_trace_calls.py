import sys
from pathlib import Path
import struct
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from map_trace_calls import PeImage, map_calls


def fixture():
    data = bytearray(0x600)
    def put(offset, fmt, *values):
        struct.pack_into('<'+fmt, data, offset, *values)
    data[:2] = b'MZ'
    put(0x3c, 'I', 0x80)
    data[0x80:0x84] = b'PE\0\0'
    put(0x84, 'HH', 0x8664, 2)
    put(0x94, 'H', 0xf0)
    opt = 0x98
    put(opt, 'H', 0x20b)
    put(opt+24, 'Q', 0x140000000)
    put(opt+56, 'I', 0x3000)
    put(opt+108, 'I', 16)
    put(opt+112+24, 'II', 0x2000, 12)
    for index, (name, rva, raw, flags) in enumerate([
            (b'.text', 0x1000, 0x200, 0x60000020),
            (b'.pdata', 0x2000, 0x400, 0x40000040)]):
        at = opt+0xf0+index*40
        data[at:at+len(name)] = name
        put(at+8, 'IIII', 0x200, rva, 0x200, raw)
        put(at+36, 'I', flags)
    put(0x400, 'III', 0x1010, 0x1030, 0x2050)
    return bytes(data)


class MappingTests(unittest.TestCase):
    def setUp(self):
        self.image = PeImage(fixture())
        self.summary = dict(metadata={}, matrix_calls=[dict(
            name='mat_view', count=1, program=7, caller_offset_from_exe_base='0x1020')])

    def test_return_address_at_range_end_uses_preceding_call(self):
        self.assertEqual(self.image.locate_return(0x1030)['unwind_ranges'][0]['begin_rva'], '0x1010')

    def test_data_or_foreign_address_is_rejected(self):
        for address in (0, 0x2010, 0x4000):
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.image.locate_return(address)

    def test_unknown_capture_identity_is_not_verified(self):
        self.assertFalse(map_calls(self.summary, self.image)['identity_verified'])

    def test_matching_capture_identity(self):
        self.summary['metadata']['exe_sha256'] = self.image.sha256
        self.assertTrue(map_calls(self.summary, self.image)['identity_verified'])

    def test_updated_game_refuses_old_capture(self):
        self.summary['metadata']['exe_sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'differs'):
            map_calls(self.summary, self.image)

    def test_truncated_image(self):
        with self.assertRaises(ValueError):
            PeImage(fixture()[:0x300])


if __name__ == '__main__':
    unittest.main()
