import copy
import math
from pathlib import Path
import struct
import unittest
from tempfile import TemporaryDirectory

from ed_model import decode_animations, decode_mesh, decode_skeleton
from export_actor import encode_actor, encode_animation_events, export_actor, flatten_vertices, pose_goldens
from test_ed_model import make_animation, make_mesh, make_skeleton


class CompactActorTests(unittest.TestCase):
    def setUp(self):
        self.parts = decode_mesh(make_mesh())
        self.bones = decode_skeleton(make_skeleton())
        self.clips = decode_animations(make_animation())

    def test_unposed_vertices_bones_and_full_root_curves_are_preserved(self):
        data = encode_actor(self.parts, self.bones, self.clips, (0,))
        self.assertEqual(struct.unpack_from("<8s3I", data), (b"EDACTOR1", 3, 2, 1))
        self.assertEqual(struct.unpack_from("<8fHH", data, 20), (0, 0, 0, 0, 0, 1, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from("<i", data, 20 + 108 + 76)[0], -1)
        clip = 20 + 108 + 160
        self.assertEqual(struct.unpack_from("<3I", data, clip), (self.clips[0]["resource_id"], 0, 2))
        self.assertEqual(struct.unpack_from("<3f", data, clip + 12), (3, 5, 7))
        self.assertEqual(struct.unpack_from("<3f", data, clip + 12 + 36), (4, 6, 8))
        self.assertEqual(len(data), 20 + 108 + 160 + 12 + 72)

    def test_invalid_skeleton_clip_and_bone_references_fail(self):
        parts, bones, clips = copy.deepcopy((self.parts, self.bones, self.clips))
        parts[0]["bone_indices"][0] = 2
        with self.assertRaises(ValueError):
            encode_actor(parts, bones, clips, (0,))
        bones[0]["parent"] = 0
        with self.assertRaises(ValueError):
            encode_actor(self.parts, bones, clips, (0,))
        with self.assertRaises(ValueError):
            encode_actor(self.parts, self.bones, clips * 2, (0,))
        clips[0]["root_positions"][0][0] = math.nan
        with self.assertRaises(ValueError):
            encode_actor(self.parts, self.bones, clips, (0,))

    def test_tagged_mesh_retains_original_part_number(self):
        data = encode_actor(self.parts * 2, self.bones, self.clips, (1,), part_tags=True)
        self.assertEqual(data[:8], b"EDACTOR2")
        self.assertEqual([struct.unpack_from("<H", data, 54 + index * 36)[0] for index in range(3)], [1, 1, 1])

    def test_original_animation_events_are_preserved_verbatim(self):
        clip=copy.deepcopy(self.clips[0]);raw36=struct.pack("<Ii7I",0x0107000C,-1,*range(7));raw20=struct.pack("<5I",1,2,3,4,5)
        clip.update(events36_raw=[raw36.hex()],events20_raw=[raw20.hex()])
        data=encode_animation_events([clip])
        self.assertEqual(data[:12],struct.pack("<8sI",b"EDANEV1\0",1))
        self.assertEqual(data[12:],struct.pack("<3I",clip["resource_id"],1,1)+raw36+raw20)
        with self.assertRaises(ValueError):encode_animation_events([clip,clip])

    def test_goldens_use_original_pose_and_retain_root_xyz(self):
        data, count = pose_goldens(self.parts, self.bones, self.clips, (0,), set())
        self.assertEqual(struct.unpack_from("<8sI", data), (b"EDPOSE1\0", 2))
        self.assertEqual(count, 2)
        self.assertEqual(struct.unpack_from("<3I3f", data, 12), (self.clips[0]["resource_id"], 0, 3, 3, 5, 7))
        self.assertEqual(struct.unpack_from("<I3f", data, 36), (0, 10, 5, 0))
        self.assertEqual(len(flatten_vertices(self.parts, (0,))), 3)

    def test_direct_export_rejects_original_directory_before_reading(self):
        game = Path(__file__).resolve().parents[1] / "game"
        with self.assertRaises(ValueError):
            export_actor(game, game / "actors", "ash")


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalSkeletonExportTests(unittest.TestCase):
    def test_real_local_resources_and_all_action_curves(self):
        game=Path(__file__).resolve().parents[1]/"game"
        with TemporaryDirectory() as directory:
            result=export_actor(game,Path(directory),"skeleton")
            self.assertEqual((result["archive"],result["texture_name"]),("al02.pc","pnsk0000.dds"))
            self.assertEqual((result["bone_count"],result["clip_count"],result["vertex_count"]),(16,27,1098))
            self.assertEqual(result["selected_parts"],[0])
            self.assertEqual(result["all_parts_count"],1)
            self.assertEqual(result["full_mesh_golden_clips"],["0x7c94f04","0x7c95404","0x7c95484"])
            root=Path(directory)/"actors/skeleton"
            self.assertEqual((root/"actor-parts.bin").read_bytes()[:8],b"EDACTOR2")
            from PIL import Image
            with Image.open(root/"texture.bmp") as texture:self.assertEqual(texture.size,(256,256))

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_real_ah05_local_actor_resources(self):
        game=Path(__file__).resolve().parents[1]/"game"
        for name,bones,clips,vertices,texture in (("hellbillyfat",16,19,1020,"pnhf0000.dds"),("hellbillythin",16,18,834,"pnhm0000.dds"),("demonpig",25,18,894,"pnpg0000.dds")):
            with self.subTest(actor=name),TemporaryDirectory() as directory:
                result=export_actor(game,Path(directory),name)
                self.assertEqual((result["archive"],result["texture_name"]),("ah05.pc",texture))
                self.assertEqual((result["bone_count"],result["clip_count"],result["vertex_count"]),(bones,clips,vertices))
                self.assertEqual((result["selected_parts"],result["all_parts_count"]),([0],1))
                from PIL import Image
                with Image.open(Path(directory)/"actors"/name/"texture.bmp") as image:self.assertEqual(image.size,(256,256))

    def test_headless_skeleton_keeps_its_distinct_mesh_and_texture(self):
        game=Path(__file__).resolve().parents[1]/"game"
        with TemporaryDirectory() as directory:
            result=export_actor(game,Path(directory),"headlessskeleton")
            self.assertEqual((result["archive"],result["mesh"],result["skeleton"],result["texture"]),("ak09.pc","0x6082000","0x6882000","0x8082000"))
            self.assertEqual((result["texture_name"],result["bone_count"],result["clip_count"],result["vertex_count"]),("pncb0000.dds",16,14,1158))
            self.assertEqual((result["selected_parts"],result["all_parts_count"]),([0],1))

    def test_troop_resources_and_verbatim_ranged_animation_event(self):
        game=Path(__file__).resolve().parents[1]/"game"
        for name,clips,vertices,texture in (("troopknife",22,762,"pnmm0000.dds"),("troopclaw",22,822,"pnmc0000.dds"),("trooprange",20,834,"pnma0000.dds")):
            with self.subTest(actor=name),TemporaryDirectory() as directory:
                result=export_actor(game,Path(directory),name)
                self.assertEqual((result["archive"],result["texture_name"]),("ac01.pc",texture))
                self.assertEqual((result["bone_count"],result["clip_count"],result["vertex_count"]),(16,clips,vertices))
                self.assertEqual((result["selected_parts"],result["all_parts_count"]),([0],1))
                data=(Path(directory)/"actors"/name/"animation-events.bin").read_bytes()
                self.assertEqual(data[:8],b"EDANEV1\0");cursor=12;records={}
                for _ in range(struct.unpack_from("<I",data,8)[0]):
                    identity,n36,n20=struct.unpack_from("<3I",data,cursor);cursor+=12
                    records[identity]=[data[cursor+36*n:cursor+36*(n+1)] for n in range(n36)]
                    cursor+=36*n36+20*n20
                self.assertEqual(cursor,len(data))
                if name=="trooprange":
                    shot=[record for record in records[0x07B011C4] if record[2]==7]
                    self.assertEqual(len(shot),1)
                    self.assertEqual(struct.unpack_from("<HBBi",shot[0]),(12,7,1,-1))

    def test_tree_pair_keeps_distinct_original_resources_and_body_parts(self):
        game=Path(__file__).resolve().parents[1]/"game"
        for name,bones,clips,vertices,parts,texture in (("treeboss",45,10,4050,[0,1,2,3],"pntr0000.dds"),("treearm",4,3,222,[0],"pnta0000.dds")):
            with self.subTest(actor=name),TemporaryDirectory() as directory:
                result=export_actor(game,Path(directory),name)
                self.assertEqual((result["archive"],result["texture_name"]),("at02.pc",texture))
                self.assertEqual((result["bone_count"],result["clip_count"],result["vertex_count"]),(bones,clips,vertices))
                self.assertEqual((result["selected_parts"],result["all_parts_count"]),(parts,len(parts)))

    def test_troop_leader_original_mesh_and_pillar_geometry_fixture(self):
        game=Path(__file__).resolve().parents[1]/"game"
        with TemporaryDirectory() as directory:
            result=export_actor(game,Path(directory),"troopleaderboss")
            self.assertEqual((result["archive"],result["texture_name"]),("ac05.pc","pnsl0000.dds"))
            self.assertEqual((result["bone_count"],result["clip_count"],result["vertex_count"]),(18,16,1422))
            self.assertEqual((result["selected_parts"],result["all_parts_count"]),([0],1))
            from export_actor import resource
            self.assertEqual((Path(directory)/"actors/troopleaderboss/source-scene.native").read_bytes(),
                             resource((game/"data/ac05.pc").read_bytes(),0x0F022800))

    def test_levela_boss_resources_keep_source_scene_part_choices(self):
        game=Path(__file__).resolve().parents[1]/"game"
        for name,scene,texture,bones,clips,vertices,parts,total in (("matriarchboss","ah06","pnmi0000.dds",32,15,2148,[0],1),
                ("deadash","ag03","psda0000.dds",25,17,2112,[0,1],2),("anniezombie","au04","pnaz0000.dds",57,26,2838,[0,1,2],4)):
            with self.subTest(actor=name),TemporaryDirectory() as directory:
                result=export_actor(game,Path(directory),name)
                self.assertEqual((result["archive"],result["texture_name"]),(scene+".pc",texture))
                self.assertEqual((result["bone_count"],result["clip_count"],result["vertex_count"]),(bones,clips,vertices))
                self.assertEqual((result["selected_parts"],result["all_parts_count"]),(parts,total))


if __name__ == "__main__":
    unittest.main()
