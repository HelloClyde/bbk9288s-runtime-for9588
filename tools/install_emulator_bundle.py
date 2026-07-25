from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


DEFAULT_BDA_TARGET = "/应用/程序/宠物单词.bda"
DEFAULT_GUEST_PROGRAM_DIRECTORY = "/应用/数据/9288s/系统/程序"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(fs, path: str) -> str:
    digest = hashlib.sha256()
    with fs.openbin(path, "r") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_directory(fs, path: str) -> None:
    current = ""
    for component in (part for part in path.split("/") if part):
        current += "/" + component
        if not fs.isdir(current):
            fs.makedir(current)


def replace_file(fs, path: str, payload: bytes) -> None:
    parent = path.rsplit("/", 1)[0] or "/"
    ensure_directory(fs, parent)
    if fs.isfile(path):
        fs.remove(path)
    with fs.openbin(path, "w") as stream:
        stream.write(payload)


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(
        description="离线把 9288S 兼容 BDA 和 EXE 程序目录一次性写入 9588 NAND。"
    )
    parser.add_argument("--emulator-root", type=Path, required=True)
    parser.add_argument("--nand", type=Path, required=True)
    parser.add_argument("--bda", type=Path, required=True)
    parser.add_argument("--program-dir", type=Path, required=True)
    parser.add_argument("--bda-target", default=DEFAULT_BDA_TARGET)
    parser.add_argument(
        "--guest-program-dir",
        default=DEFAULT_GUEST_PROGRAM_DIRECTORY,
    )
    args = parser.parse_args()

    emulator_root = args.emulator_root.resolve()
    nand = args.nand.resolve()
    bda = args.bda.resolve()
    program_dir = args.program_dir.resolve()
    for path, kind in (
        (emulator_root, "directory"),
        (nand, "file"),
        (bda, "file"),
        (program_dir, "directory"),
    ):
        exists = path.is_dir() if kind == "directory" else path.is_file()
        if not exists:
            raise SystemExit(f"missing {kind}: {path}")

    programs = sorted(
        (
            path
            for path in program_dir.iterdir()
            if path.is_file() and path.suffix.lower() == ".exe"
        ),
        key=lambda path: path.name,
    )
    if not programs:
        raise SystemExit(f"no 9288S EXE files found in: {program_dir}")

    sys.path.insert(0, str(emulator_root))
    from emu.qemu.nand_fs import mutate_nand_files

    payloads = {args.bda_target: bda.read_bytes()}
    for program in programs:
        payloads[f"{args.guest_program_dir}/{program.name}"] = (
            program.read_bytes()
        )
    expected = {
        path: {
            "size": len(payload),
            "sha256": sha256_bytes(payload),
        }
        for path, payload in payloads.items()
    }

    def operation(fs):
        for path, payload in payloads.items():
            replace_file(fs, path, payload)

    def validator(fs):
        for path, metadata in expected.items():
            if not fs.isfile(path):
                raise ValueError(f"missing installed file: {path}")
            if fs.getsize(path) != metadata["size"]:
                raise ValueError(f"installed size mismatch: {path}")
            if sha256_file(fs, path) != metadata["sha256"]:
                raise ValueError(f"installed digest mismatch: {path}")

    mutate_nand_files(nand, operation, validator=validator)
    print(
        json.dumps(
            {
                "ok": True,
                "nand": str(nand),
                "installed": expected,
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
