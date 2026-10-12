import contextlib
import io
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/yaz0-decode-encode"))
import yaz0


def packed(size, body):
    return b"Yaz0" + struct.pack(">I", size) + bytes(8) + body


class Yaz0Checks(unittest.TestCase):
    def test_literals_and_partial_groups(self):
        self.assertEqual(yaz0.decompress(packed(0, b"")), b"")
        self.assertEqual(yaz0.decompress(packed(1, b"\xffA")), b"A")
        self.assertEqual(yaz0.decompress(packed(9, b"\xffABCDEFGH\x80I")), b"ABCDEFGHI")

    def test_overlapping_references(self):
        self.assertEqual(yaz0.decompress(packed(10, b"\x80A\x70\x00")), b"A" * 10)
        self.assertEqual(yaz0.decompress(packed(24, b"\xe0ABC\x00\x02\x03")), b"ABC" * 8)
        self.assertEqual(yaz0.decompress(packed(5, b"\x80A\xf0\x00")), b"AAAAA")

    def test_truncation_and_invalid_reference(self):
        data = packed(24, b"\xe0ABC\x00\x02\x03")
        for end in range(len(data)):
            with self.subTest(end=end), self.assertRaises(RuntimeError):
                yaz0.decompress(data[:end])
        with self.assertRaises(RuntimeError):
            yaz0.decompress(packed(3, b"\x00\x10\x00"))
        with self.assertRaises(RuntimeError):
            yaz0.decompress(b"nope" + bytes(12))
        with self.assertRaises(RuntimeError):
            yaz0.yaz0(io.BytesIO(packed(9, b"")), max_output_size=8)
        with self.assertRaises(RuntimeError):
            yaz0.yaz0(io.BytesIO(packed(1, b"\x80A")), io.BytesIO(b"stale")).decompress()

    def test_existing_compression_roundtrip(self):
        for data in (b"", b"A", bytes(range(256)), b"ABCD" * 20):
            with contextlib.redirect_stdout(io.StringIO()):
                encoded = yaz0.compress(data)
            self.assertEqual(yaz0.decompress(encoded), data)
            self.assertEqual(yaz0.decompress_fileobj(io.BytesIO(encoded)).getvalue(), data)


if __name__ == "__main__":
    unittest.main()
