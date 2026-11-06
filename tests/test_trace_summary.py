import sys
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from summarize_trace import summarize


class TraceSummaryTests(unittest.TestCase):
    def test_epoch_filter_retains_uniform_names_but_excludes_menu_draws(self):
        trace = ('frame\tthread\tcontext\tevent\tprogram\tfbo\ta\tb\tc\tcaller\tdata\n'
                 '#capture_epoch\t0\n'
                 '0\t1\tctx\tuniform\t7\t0\t7\t3\t0\t10000100\tmat_view\n'
                 '0\t1\tctx\tdraw_arrays\t7\t0\t4\t0\t3\t10000200\n'
                 '#capture_epoch\t1\n'
                 '50\t1\tctx\tmatrix4\t7\t0\t3\t1\t0\t10000300\t1\n'
                 '50\t1\tctx\tdraw_arrays\t7\t0\t4\t0\t3\t10000200\n')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'capture.tsv'
            path.write_text(trace, encoding='utf-8')
            result = summarize(path, 1)
        self.assertEqual(list(result['frames']), ['50'])
        self.assertEqual(result['event_counts']['draw_arrays'], 1)
        self.assertEqual(result['matrix_calls'][0]['name'], 'mat_view')
        self.assertEqual(result['selected_epoch'], 1)

    def test_missing_uniforms_do_not_alias_each_other(self):
        # Real Stormworks pattern: multiple absent uniforms resolve to -1.
        trace = ('frame\tthread\tcontext\tevent\tprogram\tfbo\ta\tb\tc\tcaller\tdata\n'
                 '#exe_base\t0000000010000000\n'
                 '0\t1\tctx\tuniform\t7\t0\t7\t-1\t0\t10000100\tmat_world_prev\n'
                 '0\t1\tctx\tuniform\t7\t0\t7\t-1\t0\t10000100\tmat_world_next\n'
                 '0\t1\tctx\tmatrix4\t7\t0\t-1\t1\t0\t10000200\t1\n'
                 '0\t1\tctx\tuniform\t7\t0\t7\t3\t0\t10000100\tmat_view_proj\n'
                 '0\t1\tctx\tmatrix4\t7\t0\t3\t1\t0\t10000300\t1\n')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'capture.tsv'
            path.write_text(trace, encoding='utf-8')
            result = summarize(path)
        self.assertEqual(result['ignored_matrix_calls'], 1)
        self.assertEqual(len(result['matrix_calls']), 1)
        self.assertEqual(result['matrix_calls'][0]['name'], 'mat_view_proj')
        self.assertEqual(result['matrix_calls'][0]['caller_offset_from_exe_base'], '0x300')


if __name__ == '__main__':
    unittest.main()
