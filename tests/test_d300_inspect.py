from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "d300_inspect", ROOT / "tools" / "d300_inspect.py"
)
assert SPEC and SPEC.loader
D300 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = D300
SPEC.loader.exec_module(D300)


def make_image() -> bytes:
    data = bytearray(0x330)
    data[:4] = b"D300"
    struct.pack_into("<II", data, 0x04, 0x80, len(data))
    data[0x0C:0x10] = "游戏".encode("gb18030")
    struct.pack_into("<I", data, 0x14, 0x100)
    data[0x40:0x48] = b"BBK LTD."
    struct.pack_into("<I", data, 0x50, 0x12345678)
    data[0x80:0x84] = b"EXE\0"
    struct.pack_into("<III", data, 0x84, 0x80, 0x100, 0x210)
    struct.pack_into("<II", data, 0x98, 0x310, 0x20)
    data[0xB0:0xB4] = "测试".encode("gb18030")
    data[0x310:0x31D] = b"PirateshipApp"
    return bytes(data)


class D300InspectTests(unittest.TestCase):
    def test_parse_segments(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.exe"
            path.write_bytes(make_image())
            image, data = D300.D300Image.parse(path)

        self.assertEqual(image.title, "测试")
        self.assertEqual(image.category, "游戏")
        self.assertEqual(image.program_offset, 0x310)
        self.assertEqual(image.program_size, 0x20)
        self.assertEqual(image.icon_size, 0x210)
        self.assertEqual(image.appended_size, 0)
        self.assertEqual(len(data), 0x330)

    def test_accepts_appended_assets(self) -> None:
        data = make_image() + b"APPENDED-ASSETS"
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "packed.exe"
            path.write_bytes(data)
            image, parsed_data = D300.D300Image.parse(path)

        self.assertEqual(image.declared_size, 0x330)
        self.assertEqual(image.appended_size, len(b"APPENDED-ASSETS"))
        self.assertEqual(parsed_data[0x330:], b"APPENDED-ASSETS")

    def test_rejects_declared_size_beyond_file(self) -> None:
        data = bytearray(make_image())
        struct.pack_into("<I", data, 0x08, len(data) + 1)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "truncated.exe"
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                D300.D300Image.parse(path)

    def test_strings(self) -> None:
        strings = dict(D300.iter_strings(make_image()))
        self.assertEqual(strings[0x310], "PirateshipApp")

    def test_rejects_bad_range(self) -> None:
        data = bytearray(make_image())
        struct.pack_into("<I", data, 0x9C, 0x1000)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.exe"
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                D300.D300Image.parse(path)


if __name__ == "__main__":
    unittest.main()
