from pathlib import Path
import hashlib
import json
import struct
import tempfile
import unittest
from unittest.mock import patch

from ed_dc_vfx import ATLAS_SHA, SHA, SUPPORTED, encode_files, export_vfx, parse_effects

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/dreamcast-original-20261027/files/1ST_READ.BIN'
ATLAS = ROOT / 'build/dreamcast-objects-candidate-20261028/A/objects/textures/0816e044.rgba'


@unittest.skipUnless(BINARY.is_file() and ATLAS.is_file(), 'Recorded original Dreamcast inputs required')
class DreamcastVfxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = BINARY.read_bytes()
        cls.parsed = parse_effects(cls.source)

    def test_native_base_constructors_and_pool(self):
        d = self.parsed['definitions']
        self.assertEqual(hashlib.sha256(self.source).hexdigest(), SHA)
        self.assertEqual(self.parsed['pool'], 1500)
        # Independently read in Ghidra: c090f40 pools c0910c8/c0910cc;
        # sprite-grid opcode c091020 selects case4 (2x2) in c08c0a0.
        self.assertEqual((d[9]['cap'], d[9]['life'], d[9]['size_min'], d[9]['size_max']), (100,40,75.,125.))
        self.assertEqual((d[9]['columns'], d[9]['rows'], d[9]['delta']), (2,2,[0,0,0,-3]))
        # Constructor c0936e0 pools c093818..24 identify the DC tile.
        self.assertEqual(d[23]['uv'], [.125,.75,.25,.875])
        self.assertEqual((d[23]['life'], d[23]['growth'], d[23]['color']), (16,12.,[128,232,255,255]))
        self.assertEqual(set(d), SUPPORTED)

    def test_native_dispatch_and_distinct_child_chains(self):
        d = self.parsed['definitions']
        self.assertEqual((d[2]['count_min'],d[2]['count_max'],d[2]['direction_scale']), (20,36,.5))
        self.assertEqual((d[15]['count_min'],d[15]['count_max'],d[15]['direction_scale']), (16,24,1.))
        self.assertEqual((d[16]['count_min'],d[16]['count_max'],d[16]['children']), (8,16,[dict(flags=4,kind=9,chance=5)]))
        self.assertEqual((d[20]['child_death'],d[20]['random_child_chance']), (18,0))
        self.assertEqual((d[18]['life'],d[18]['columns'],d[18]['rows']), (270,4,4))
        self.assertEqual((d[32]['velocity'],d[32]['direction_scale']), ([0.,0.,0.],1.))
        self.assertEqual(d[7]['periodic_child'],22)

    def test_native_mode_rows_compact_without_color_loss(self):
        self.assertEqual(self.parsed['ring_table'],'0x8c0d041c')
        self.assertEqual(self.parsed['corona_table'],'0x8c0d064c')
        for ring in self.parsed['rings'].values():
            raw=bytes.fromhex(ring['native_row'])
            self.assertEqual(ring['color'],list(struct.unpack_from('<3I',raw,24)))
            self.assertEqual(ring['segments'],raw[38])
        for corona in self.parsed['coronas'].values():
            raw,compact=map(bytes.fromhex,(corona['native_row'],corona['raw']))
            self.assertEqual(compact[:8],raw[:8])
            self.assertEqual(list(compact[8:11]),list(struct.unpack_from('<3I',raw,8)))
            self.assertEqual(compact[12:],raw[20:])

    def test_export_never_invokes_pc_parsers_and_keeps_pinned_native_atlas(self):
        from contextlib import ExitStack
        functions=('ed_vfx.parse_effects','ed_vfx_hits.parse_hit_effects','ed_vfx_emitters.parse_emitter_effects',
            'ed_vfx_ranged.parse_ranged_effects','ed_vfx_tree.parse_tree_effects','ed_vfx_ash.parse_ash_effects',
            'ed_vfx_variants.parse_variant_effects','ed_vfx_npc.parse_npc_effects')
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            for name in functions:stack.enter_context(patch(name,side_effect=AssertionError('PC parser called')))
            result=export_vfx(BINARY,ATLAS,directory)
            out=Path(directory)/'vfx'
            self.assertEqual(len(result['files']),10)
            self.assertEqual(hashlib.sha256((out/'atlas.rgba').read_bytes()).hexdigest(),ATLAS_SHA)
            self.assertEqual((out/'effects.bin').read_bytes(),encode_files(self.parsed)['effects.bin'])
            self.assertFalse(any('goldens' in name for name in result['files']))
            self.assertEqual(json.loads((out/'source.json').read_text())['source_sha256'],SHA)
            # A second identical export is safe; damaged generated output is rejected.
            export_vfx(BINARY,ATLAS,directory)
            (out/'npc-effects.bin').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'overwrite differing'):
                export_vfx(BINARY,ATLAS,directory)

    def test_changed_binary_is_rejected_before_constructor_execution(self):
        changed=bytearray(self.source);changed[0x90fc8-0x10000]^=1
        with self.assertRaisesRegex(ValueError,'Unknown Dreamcast executable'):
            parse_effects(changed)

    def test_native_material_initializer_exports_exact_source_pinned_blends(self):
        materials=self.parsed['materials']
        self.assertEqual(len(materials),37)
        additive={0,3,7,9,23,24,25}
        for m in materials:
            self.assertEqual(m['words'][20],8)
            self.assertEqual(m['words'][21],10 if m['kind'] in additive else 6)
        data=encode_files(self.parsed)['render-states.bin']
        self.assertEqual(struct.unpack_from('<8s32sI',data),(b'EDVFXD1\0',bytes.fromhex(SHA),19))
        rows=list(struct.iter_unpack('<4I',data[44:]))
        self.assertEqual({k for k,s,d,callback in rows},SUPPORTED)
        self.assertEqual({k for k,s,d,callback in rows if d==10},additive)
        self.assertEqual(materials[3]['callback'],materials[7]['callback'])
        self.assertEqual({materials[k]['callback'] for k in (0,6,9,16,32)},{'0x8c0904c0'})


class OutputGuardTests(unittest.TestCase):
    def test_original_game_output_is_rejected_without_inputs_or_directory_creation(self):
        output=ROOT/'game'/'__must_never_be_created__'
        self.assertFalse(output.exists())
        with self.assertRaisesRegex(ValueError,'outside original'):
            export_vfx(ROOT/'missing-input.bin',ROOT/'missing-atlas.rgba',output)
        self.assertFalse(output.exists())


if __name__=='__main__':unittest.main()
