import gzip
from timeit import timeit
import unittest
import zlib_state

class TestZlibState(unittest.TestCase):
    def test_GzipStateFile_fullextract(self):
        with zlib_state.GzipStateFile('testdata/frankenstein.txt.gz') as f:
            all_text = f.read()
        with gzip.open('testdata/frankenstein.txt.gz', 'rb') as f:
            source_text = f.read()
        self.assertEqual(all_text, source_text)

    def test_compare_times(self):
        def gzip_run():
            with gzip.open('testdata/frankenstein.txt.gz', 'rb') as f:
                all_text = f.read()
        def gzip_state_run():
            with zlib_state.GzipStateFile('testdata/frankenstein.txt.gz') as f:
                all_text = f.read()
        def gzip_state_run_keep_last():
            with zlib_state.GzipStateFile('testdata/frankenstein.txt.gz', keep_last_state=True) as f:
                all_text = f.read()
        def zlib_state_run():
            decomp = zlib_state.Decompressor(32 + 15)
            with open('testdata/frankenstein.txt.gz', 'rb') as f:
                while not decomp.eof():
                    needed_input = decomp.needs_input()
                    if needed_input > 0:
                        decomp.feed_input(f.read(needed_input))
                    decomp.read()
        # Warm up cache
        gzip_run()
        gzip_state_run()
        gzip_state_run_keep_last()
        zlib_state_run()
        print("gzip_run", timeit(gzip_run, number=100), "s/100")
        print("gzip_state_run", timeit(gzip_state_run, number=100), "s/100")
        print("gzip_state_run_keep_last", timeit(gzip_state_run_keep_last, number=100), "s/100")
        print("zlib_state_run", timeit(zlib_state_run, number=100), "s/100")
        # It doesn't look like my buffered implementation is quite as fast as gzip's
        # Keeping the last sate also hurts a bit. But when not using the buffered version, it's faster!
        # gzip_run 0.23395611305022612 s/100
        # gzip_state_run 0.294685336004477 s/100
        # gzip_state_run_keep_last 0.316738452995196 s/100
        # zlib_state_run 0.1936574190040119 s/100

    def test_GzipStateFile_zseek(self):
        TARGET_LINE = 5000 # pick back up after around the 5,000th line
        with zlib_state.GzipStateFile('testdata/frankenstein.txt.gz', keep_last_state=True) as f:
            for i, line in enumerate(f):
                if i == TARGET_LINE:
                    state, pos = f.last_state, f.last_state_pos

        with zlib_state.GzipStateFile('testdata/frankenstein.txt.gz', keep_last_state=True) as f:
            f.zseek(pos, state)
            remainder = f.read()
            # The 5,000th line should be there
            self.assertTrue(b'day.  He pointed out to me the shifting colours of the landscape and' in remainder)
            self.assertEqual(len(remainder), 207371)


if __name__ == '__main__':
    unittest.main()
