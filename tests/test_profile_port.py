"""Regression checks for the offline native-profile comparison tool."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_profile_port import Audit, Decoder, ROOT


class Image:
    def __init__(self, name, data, start=0x1000):
        self.path, self.data, self.start = name, data, start

    def read(self, at, size):
        return self.data[at-self.start:at-self.start+size]

    def section(self, at):
        return (self.start, len(self.data), 0, len(self.data), '.text')


@unittest.skipUnless((ROOT/'build/capture/Release/swvr_profile_decoder.dll').exists(),
                     'Build swvr_profile_decoder in Release on Windows first')
class ProfilePortTests(unittest.TestCase):
    def setUp(self):
        self.decoder = Decoder(ROOT/'build/capture/Release/swvr_profile_decoder.dll')

    def compare(self, a, b):
        old, new = Image('old', a), Image('new', b)
        return Audit(old, new, self.decoder).equivalent(0x1000,0x1000+len(a),0x1000,0x1000+len(b))

    def test_unknown_object_field_is_not_masked(self):
        self.assertFalse(self.compare(bytes.fromhex('488b4140c3'),bytes.fromhex('488b4148c3')))

    def test_explicit_owner_field_migration(self):
        self.assertTrue(self.compare(bytes.fromhex('488b81b0e60c00c3'),bytes.fromhex('488b81b8e60c00c3')))
        self.assertFalse(self.compare(bytes.fromhex('488b81b0e60c00c3'),bytes.fromhex('488b81c0e60c00c3')))

    def test_register_and_constant_changes_are_rejected(self):
        self.assertFalse(self.compare(bytes.fromhex('488b4140c3'),bytes.fromhex('488b5140c3')))
        self.assertFalse(self.compare(bytes.fromhex('b801000000c3'),bytes.fromhex('b802000000c3')))

    def test_relative_destination_remains_available_for_validation(self):
        old=Image('call',bytes.fromhex('e810000000c3'))
        normalized, refs=self.decoder.normalize(old,0x1000,0x1006)
        self.assertEqual(normalized,bytes.fromhex('e800000000c3'))
        self.assertEqual(refs[0][4],0x1015)

    def test_truncated_instruction_is_rejected(self):
        with self.assertRaises(ValueError):
            self.decoder.normalize(Image('truncated',b'\xe8\x01'),0x1000,0x1002)

    def test_zero_initialized_global_needs_native_identity(self):
        class DataImage(Image):
            def section(self,at):
                return (self.start,len(self.data),0,len(self.data),'.data')
        old,new=DataImage('old-bss',bytes(16)),DataImage('new-bss',bytes(16))
        report=Audit(old,new,self.decoder).run([0x1000])
        self.assertTrue(any('no native reference' in error for error in report['errors']))
        self.assertFalse(report['mapping'])

    def test_switch_entries_and_byte_indices_are_checked(self):
        a=b'\xc3\x90\x90\x90'+struct.pack('<3I',0x1000,0x1001,0x1002)+b'\0\1\2\0'
        b=b'\xc3\x90\x90\x90'+struct.pack('<3I',0x2000,0x2001,0x2002)+b'\0\1\2\0'
        old,new=Image('old-table',a),Image('new-table',b,0x2000)
        audit=Audit(old,new,self.decoder)
        self.assertTrue(audit.equivalent(0x1000,0x1000+len(a),0x2000,0x2000+len(b)))
        bad=Image('bad-table',b[:-1]+b'\1',0x2000)
        self.assertFalse(Audit(old,bad,self.decoder).equivalent(0x1000,0x1000+len(a),0x2000,0x2000+len(b)))


if __name__ == '__main__':
    unittest.main()
