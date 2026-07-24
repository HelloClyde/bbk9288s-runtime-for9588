#!/usr/bin/env python3
"""Inspect a BBK 9288S D300 application without modifying it."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


MAGIC = b"D300"
MIN_HEADER_SIZE = 0xC0


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def c_string(data: bytes, start: int, end: int, encoding: str = "gb18030") -> str:
    raw = data[start:end].split(b"\0", 1)[0]
    return raw.decode(encoding, errors="replace")


@dataclass(frozen=True)
class D300Image:
    path: str
    file_size: int
    header_size: int
    declared_size: int
    appended_size: int
    category: str
    flags: int
    vendor: str
    signature: int
    title: str
    icon_offset: int
    icon_size: int
    program_offset: int
    program_size: int
    resource_offset: int
    resource_size: int
    sha256: str

    @classmethod
    def parse(cls, path: Path) -> tuple["D300Image", bytes]:
        data = path.read_bytes()
        if len(data) < MIN_HEADER_SIZE:
            raise ValueError(f"{path}: file is too short for a D300 header")
        if data[:4] != MAGIC:
            raise ValueError(f"{path}: expected D300 magic, got {data[:4]!r}")

        image = cls(
            path=str(path),
            file_size=len(data),
            header_size=u32(data, 0x04),
            declared_size=u32(data, 0x08),
            appended_size=len(data) - u32(data, 0x08),
            category=c_string(data, 0x0C, 0x14),
            flags=u32(data, 0x14),
            vendor=c_string(data, 0x40, 0x50, encoding="ascii"),
            signature=u32(data, 0x50),
            title=c_string(data, 0xB0, 0xC0),
            icon_offset=u32(data, 0x88),
            icon_size=u32(data, 0x8C),
            program_offset=u32(data, 0x98),
            program_size=u32(data, 0x9C),
            resource_offset=u32(data, 0xA0),
            resource_size=u32(data, 0xA4),
            sha256=hashlib.sha256(data).hexdigest(),
        )
        image.validate()
        return image, data

    def validate(self) -> None:
        if self.declared_size > self.file_size:
            raise ValueError(
                f"declared size 0x{self.declared_size:x} "
                f"exceeds file size 0x{self.file_size:x}"
            )
        if self.header_size < 0x80 or self.header_size > self.declared_size:
            raise ValueError(f"invalid header size 0x{self.header_size:x}")
        for name, offset, size in (
            ("icon", self.icon_offset, self.icon_size),
            ("program", self.program_offset, self.program_size),
            ("resource", self.resource_offset, self.resource_size),
        ):
            if size == 0:
                continue
            if (
                offset > self.declared_size
                or size > self.declared_size - offset
            ):
                raise ValueError(
                    f"{name} range 0x{offset:x}+0x{size:x} "
                    "exceeds declared image"
                )


def iter_strings(data: bytes, minimum: int = 4):
    start = 0
    size = len(data)
    while start < size:
        end = data.find(b"\0", start, min(size, start + 1024))
        if end < 0:
            break
        raw = data[start:end]
        if len(raw) >= 2:
            try:
                text = raw.decode("gb18030")
            except UnicodeDecodeError:
                text = ""
            if text and all(ch.isprintable() or ch in "\r\n\t" for ch in text):
                has_cjk = any("\u3400" <= ch <= "\u9fff" for ch in text)
                is_ascii = all(ord(ch) < 128 for ch in text)
                if has_cjk or (is_ascii and len(text) >= minimum):
                    yield start, text
        start = end + 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strings", action="store_true")
    parser.add_argument("--extract-dir", type=Path)
    args = parser.parse_args()

    image, data = D300Image.parse(args.image)
    info = asdict(image)
    if args.json:
        print(json.dumps(info, ensure_ascii=False, indent=2))
    else:
        for key, value in info.items():
            if isinstance(value, int):
                print(f"{key:16} 0x{value:x} ({value})")
            else:
                print(f"{key:16} {value}")

    if args.strings:
        print("\nstrings:")
        for offset, text in iter_strings(data):
            print(f"0x{offset:06x} {text!r}")

    if args.extract_dir:
        args.extract_dir.mkdir(parents=True, exist_ok=True)
        segments = {
            "icon.bin": (image.icon_offset, image.icon_size),
            "program.bin": (image.program_offset, image.program_size),
            "resource.bin": (image.resource_offset, image.resource_size),
        }
        for name, (offset, size) in segments.items():
            if size:
                (args.extract_dir / name).write_bytes(data[offset : offset + size])
                print(f"wrote {args.extract_dir / name}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
