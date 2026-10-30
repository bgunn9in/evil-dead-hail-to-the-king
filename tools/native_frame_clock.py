"""Bounded CPU evaluator for the PC executable's 33 millisecond frame gate."""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PERIOD_MS = 33


def _u32(value):
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise ValueError("Native clock samples must be uint32 milliseconds")
    return value


@dataclass
class FrameGate:
    """One accepted iteration at most; the original never catches up lost ticks."""

    last: int

    def __post_init__(self):
        _u32(self.last)

    def offer(self, now):
        now = _u32(now)
        if ((now - self.last) & 0xFFFFFFFF) < PERIOD_MS:
            return False
        self.last = now
        return True


def audit(game):
    import capstone
    import pefile

    data = (Path(game) / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    expected = {
        0x420DF1: ("mov", "esi, 0x21"),
        0x420DF6: ("call", "0x419400"),
        0x420E06: ("sub", "eax, ecx"),
        0x420E0D: ("call", "0x4314b0"),
        0x420E12: ("cmp", "dword ptr [0x439d350], esi"),
        0x420E18: ("jb", "0x420df6"),
        0x420E27: ("mov", "dword ptr [0x439d34c], ecx"),
        0x420E30: ("call", "0x420690"),
        0x420FDC: ("mov", "esi, 0x21"),
        0x40CE18: ("call", "0x432970"),
        0x40CE2D: ("call", "0x432970"),
        0x40CE3E: ("call", "0x4060e0"),
        0x40CE8D: ("call", "0x4325e0"),
        0x4325E7: ("inc", "ecx"),
        0x4325E9: ("mov", "dword ptr [0x433be68], ecx"),
    }
    for address, instruction in expected.items():
        row = next(decoder.disasm(pe.get_data(address - base, 15), address), None)
        if row is None or (row.mnemonic, row.op_str) != instruction:
            raise ValueError(f"Native frame gate changed at{address:08X}")
    result = {"format": "ED-NATIVE-FRAME-CLOCK1", "executable_sha256": hashlib.sha256(data).hexdigest(),
              "original_executable_run": False, "period_ms": PERIOD_MS,
              "nominal_authored_fps": 30, "ideal_fast_machine_ceiling_hz": 1000 / PERIOD_MS,
              "gate": "Unsigned32(now-last)>=33. Set last=now before frame work. Pump Win32 messages while waiting; no Sleep or elapsed-time catch-up.",
              "slow_frame_behavior": "A long frame produces one subsequent update, not multiple lost updates. Original frame-count timers therefore slow with rendering below the ceiling.",
              "frame_order": ["Main.processFrame()V", "Interface.processFrame()V",
                              "mode0: animation4060E0", "mode0: actor physics4031C0", "mode0: object update40E3A0",
                              "mode0: actor callbacks406560", "sound418040", "messages40D3D0",
                              "scheduler4325E0 increments sourceFrame once, runs with mask0 or1",
                              "mode0: VFX411C00 and route414D00/412D60", "camera/portal416320", "render"],
              "gates": "420690 requires available D3D device and successful BeginScene before calling40CDB0. Quit skips normal frame. A render-only420690(1), used by fades, skips40CDB0 but still invokes scene/camera worker; it does not represent a VM/animation tick.",
              "animation": "40624C..25D copies previous playback frame then adds the playback's signed step once. It is not multiplied by elapsed milliseconds.",
              "scheduler": "4325E0 increments433BE68 once even with pause mask1; sleeping threads have their own mask/deadline predicates. Do not infer all threads run while UI is active.",
              "camera_authoring": "Camera period calculation uses signed30/FPS, independent of the physical33ms loop gate.",
              "modern_runtime_limit": "A fixed33ms simulation step reproduces the fast-machine gate interval. Accumulating and catching up overdue updates is an intentional modern scheduling difference, not the original slow-frame behavior.",
              "native_ranges": []}
    for start, end in ((0x420DF1, 0x420E38), (0x420FDC, 0x421014),
                       (0x40CDB0, 0x40CEE4), (0x4325E0, 0x432675),
                       (0x40624C, 0x406263), (0x4314B0, 0x431513)):
        raw = pe.get_data(start - base, end - start)
        result["native_ranges"].append({"start_va": f"0x{start:08X}", "end_va_exclusive": f"0x{end:08X}",
                                        "file_offset": pe.get_offset_from_rva(start - base),
                                        "sha256": hashlib.sha256(raw).hexdigest()})
    gate = FrameGate(0)
    result["cpu_golden"] = [{"milliseconds": t, "accepted": gate.offer(t)}
                            for t in (0, 1, 32, 33, 34, 65, 66, 200, 201, 232, 233)]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/native-frame-clock-format.json")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == args.game.resolve() or args.game.resolve() in output.parents:
        raise ValueError("Evidence must be outside original game directory")
    result = audit(args.game)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"period={PERIOD_MS}ms, ceiling={1000 / PERIOD_MS:.9f}Hz; native predicates verified")
