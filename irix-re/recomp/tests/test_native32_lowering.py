#!/usr/bin/env python3
"""Regression check for the fail-closed static native32 control-flow mode."""
import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "recomp_linked_stack.py"
SPEC = importlib.util.spec_from_file_location("recomp_linked_stack", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
CORE_DECODER_PATH = Path(__file__).resolve().parents[1] / "tools" / "translate_n32_core.py"
CORE_SPEC = importlib.util.spec_from_file_location("translate_n32_core", CORE_DECODER_PATH)
CORE_DECODER = importlib.util.module_from_spec(CORE_SPEC)
CORE_SPEC.loader.exec_module(CORE_DECODER)


class Native32LoweringTest(unittest.TestCase):
    def test_computed_local_jump_targets_are_native_dispatch_entries(self):
        # `jr $t0` followed by a switch case and return. The indirect target
        # is data-driven, so its in-function case cannot be inferred from the
        # JR encoding alone.
        body = MODULE.render_function(
            "probe", "test", "probe", 0x1000,
            [0x01000008, 0x00000000, 0x24020007, 0x03E00008, 0x00000000],
            no_interpreter_fallback=True,
        )
        self.assertIn("case 0x00001008u: goto L_00001008;", body)
        self.assertIn("L_00001008:", body)

    def test_mips3_hi_lo_transfers_keep_full_n32_register_values(self):
        # MTHI $v0 and MTLO $v0. N32 GPRs and HI/LO are 64-bit.
        self.assertEqual(CORE_DECODER.decode(0x1000, 0x00400011),
                         (["cpu->hi = cpu->gpr[2] /* v0 */;"], None))
        self.assertEqual(CORE_DECODER.decode(0x1004, 0x00400013),
                         (["cpu->lo = cpu->gpr[2] /* v0 */;"], None))

    def test_unsupported_control_traps_without_interpreter_fallback(self):
        # A truncated JR has no in-function delay slot, which exercises the
        # translator's former interpreter escape path.
        body = MODULE.render_function(
            "probe", "test", "probe", 0x1000, [0x03E00008],
            no_interpreter_fallback=True,
        )
        self.assertIn("irix_guest_trap(rt, cpu, 0xeeee0004u)", body)
        self.assertNotIn("irix_mips_interp_call", body)

    def test_default_mode_keeps_existing_runner_behavior(self):
        body = MODULE.render_function("probe", "test", "probe", 0x1000, [0x03E00008])
        self.assertIn("irix_mips_interp_call(rt, cpu, pc)", body)


if __name__ == "__main__":
    unittest.main()
