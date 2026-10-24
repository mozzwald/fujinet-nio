import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from stream_timing import Window


class StreamTests(unittest.TestCase):
    def test_edge_across_chunks_and_exact_window(self):
        w=Window(5)
        w.feed(b'\xff'*100000)
        self.assertEqual(w.data,b'')
        w.feed(b'\xfe\xfc')
        self.assertEqual(w.data,b'\xff\xfe\xfc')
        w.feed(b'\xfd\xff\xff')
        self.assertTrue(w.complete)
        self.assertEqual(w.data,b'\xff\xfe\xfc\xfd\xff')

    def test_first_low_is_not_a_falling_edge(self):
        w=Window(3);w.feed(b'\x00'*100);self.assertFalse(w.triggered)
        w.feed(b'\x01\x00\x01');self.assertEqual(w.data,b'\x01\x00\x01')

    def test_other_channels_do_not_trigger_and_memory_is_bounded(self):
        w=Window(3,1);w.feed(b'\xff\xfe'*100000)
        self.assertFalse(w.triggered);self.assertEqual(w.previous,b'\xfe')
        w.feed(b'\xfc\xff'*100000)
        self.assertTrue(w.complete);self.assertEqual(len(w.data),3)

if __name__ == '__main__': unittest.main()
