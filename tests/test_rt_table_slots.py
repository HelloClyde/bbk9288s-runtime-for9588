from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "rt_table_slots", ROOT / "tools" / "rt_table_slots.py"
)
assert SPEC and SPEC.loader
SLOTS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SLOTS
SPEC.loader.exec_module(SLOTS)


class RelocationTableSlotTests(unittest.TestCase):
    def test_nested_callback_is_not_a_slot(self) -> None:
        source = """
            typedef struct {
                int (*MessageBox)(int value);
                int (*RegisterDialogFunc)(void (*func)());
                void (*UnRegisterDialogFunc)();
            } T_GUI_RelocationTable;
        """

        slots = SLOTS.parse_slots(source)

        self.assertEqual(
            [(slot.index, slot.offset, slot.name) for slot in slots],
            [
                (0, 0x00, "MessageBox"),
                (1, 0x04, "RegisterDialogFunc"),
                (2, 0x08, "UnRegisterDialogFunc"),
            ],
        )

    def test_multiline_declaration_is_one_slot(self) -> None:
        source = """
            typedef struct {
                int (*Draw)(int x,
                            int y);
            } T_GUI_RelocationTable;
        """

        slots = SLOTS.parse_slots(source)

        self.assertEqual(slots[0].name, "Draw")
        self.assertEqual(slots[0].declaration, "int (*Draw)(int x, int y)")


if __name__ == "__main__":
    unittest.main()
