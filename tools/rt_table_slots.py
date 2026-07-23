from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


FIELD_NAME = re.compile(r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")


@dataclass(frozen=True)
class Slot:
    index: int
    name: str
    declaration: str

    @property
    def offset(self) -> int:
        return self.index * 4


def _struct_body(source: str) -> str:
    match = re.search(r"\btypedef\s+struct\b[^{]*\{", source)
    if not match:
        raise ValueError("no typedef struct body found")

    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        character = source[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise ValueError("unterminated typedef struct body")


def _top_level_declarations(body: str) -> Iterable[str]:
    start = 0
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0

    for index, character in enumerate(body):
        if character == "(":
            paren_depth += 1
        elif character == ")":
            paren_depth -= 1
        elif character == "[":
            bracket_depth += 1
        elif character == "]":
            bracket_depth -= 1
        elif character == "{":
            brace_depth += 1
        elif character == "}":
            brace_depth -= 1
        elif (
            character == ";"
            and paren_depth == 0
            and bracket_depth == 0
            and brace_depth == 0
        ):
            declaration = " ".join(body[start:index].split())
            start = index + 1
            if declaration:
                yield declaration

        if paren_depth < 0 or bracket_depth < 0 or brace_depth < 0:
            raise ValueError("unbalanced declaration delimiters")

    if paren_depth or bracket_depth or brace_depth:
        raise ValueError("unbalanced declaration delimiters")


def parse_slots(preprocessed_source: str) -> list[Slot]:
    slots: list[Slot] = []
    for declaration in _top_level_declarations(_struct_body(preprocessed_source)):
        match = FIELD_NAME.search(declaration)
        if not match:
            continue
        slots.append(Slot(len(slots), match.group(1), declaration))
    if not slots:
        raise ValueError("no function-pointer fields found")
    return slots


def preprocess(header: Path, definitions: list[str]) -> str:
    command = ["gcc", "-E", "-P", "-x", "c"]
    command.extend(f"-D{definition}" for definition in definitions)
    command.append(str(header))
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "gcc preprocessing failed")
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(
        description="List 32-bit function-pointer slots in a 9288S RT_table header."
    )
    parser.add_argument("header", type=Path)
    parser.add_argument(
        "-D",
        "--define",
        action="append",
        default=[],
        help="preprocessor symbol (repeatable)",
    )
    parser.add_argument("--slot", type=int, help="show only this decimal slot")
    parser.add_argument("--find", help="show slots whose names contain this text")
    args = parser.parse_args()

    slots = parse_slots(preprocess(args.header, args.define))
    selected = slots
    if args.slot is not None:
        selected = [slot for slot in selected if slot.index == args.slot]
    if args.find:
        needle = args.find.casefold()
        selected = [slot for slot in selected if needle in slot.name.casefold()]

    for slot in selected:
        print(f"{slot.index:3d}  +0x{slot.offset:03x}  {slot.name}")
    if (args.slot is not None or args.find) and not selected:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
