import sys
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from map_trace_calls import PeImage
from verify_camera_probe import verify
from test_map_trace_calls import fixture


class ProbeTests(unittest.TestCase):
    def run_capture(self, wrong_product=False, wrong_identity=False, enabled=True, epoch=None):
        image = PeImage(fixture())
        view = [1,0,0,0,0,1,0,0,0,0,-1,0,3,4,5,1]
        projection = [2,0,0,0,0,2,0,0,0,0,-1.1,-1,0,0,-.2,0]
        vp = [2,0,0,0,0,2,0,0,0,0,1.1,1,6,8,-5.7,-5]
        if wrong_product:
            vp[12] = 99
        lines = ['frame\tthread\tcontext\tevent\tprogram\tfbo\ta\tb\tc\tcaller\tdata',
                 '#exe_base\t140000000',
                 '#exe_sha256\t'+('0'*64 if wrong_identity else image.sha256),
                 '#camera_probe\t'+('enabled' if enabled else 'unsupported_executable')]
        for kind, matrix in [('view', view), ('projection', projection), ('view_projection', vp)]:
            lines.append('0\t1\tctx\tcamera_'+kind+'\t0\t0\t12345\t0\t0\t140001020\t'+
                         '\t'.join(map(str, matrix)))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'trace.tsv'
            path.write_text('\n'.join(lines)+'\n', encoding='utf-8')
            return verify(path, image, epoch)

    def test_consistent_reflected_camera(self):
        result = self.run_capture()
        self.assertTrue(result['passed'])
        self.assertEqual(result['samples'], 1)

    def test_inconsistent_product_rejected(self):
        self.assertFalse(self.run_capture(wrong_product=True)['passed'])

    def test_wrong_build_rejected(self):
        self.assertFalse(self.run_capture(wrong_identity=True)['passed'])

    def test_disabled_hook_rejected(self):
        self.assertFalse(self.run_capture(enabled=False)['passed'])

    def test_missing_epoch_does_not_reuse_menu_success(self):
        report = self.run_capture(epoch=1)
        self.assertFalse(report['passed'])
        self.assertEqual(report['samples'], 0)


if __name__ == '__main__':
    unittest.main()
