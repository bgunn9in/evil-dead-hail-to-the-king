"""Bounded original-JPC evidence for the earned LevelB story fixtures."""
import hashlib
import json
from pathlib import Path

from ed_scripts import ScriptPackage, _integer
from ed_vm import Interpreter, Object

ROOT = Path(__file__).resolve().parents[1]


def audit(game):
    raw = (Path(game) / "data/script/pc/levelB.jpc").read_bytes()
    package = ScriptPackage(raw)
    vm = Interpreter(package)
    selected = (("SceneBM01", "activateScene", "(Z)V"),
                ("SceneBM01", "allowUseItem", "(LAsh;II)I"),
                ("SceneBM01", "allowUse", "(LAsh;I)I"),
                ("SceneBM01", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"),
                ("SceneBM01", "reportPickupItem", "(Led/base/EDNpc;I)V"),
                ("SceneBM01", "processFrame", "()V"),
                ("SceneBM01", "reportCameraState", "(II)V"),
                ("SceneBM01", "spawnMonster1", "(LGenericNpc;III)V"),
                ("SceneBM01", "spawnDeathMonster1", "(LGenericNpc;III)V"),
                ("SceneBM01", "spawnWaveActivateMonster1", "(II)V"),
                ("PoopCommander", "actionPoopPuzzle", "(LGenericNpc;)Z"),
                ("PoopCommander", "doPuzzlePoop", "(LGenericNpc;I)Z"),
                ("Ash", "inventoryCombine", "(Led/base/EDInventoryItem;Led/base/EDInventoryItem;)I"),
                ("SceneBM02", "spawnDeathMonster1", "(LGenericNpc;III)V"),
                ("SceneBM02", "allowUse", "(LAsh;I)I"),
                ("SceneBM11", "processFrame", "()V"),
                ("SceneBM11", "allowUse", "(LAsh;I)I"),
                ("SceneBM11", "reportPickupItem", "(Led/base/EDNpc;I)V"),
                ("SceneBT01", "activateScene", "(Z)V"),
                ("SceneBT01", "allowUse", "(LAsh;I)I"),
                ("SceneBT01", "reportPickupItem", "(Led/base/EDNpc;I)V"),
                ("SceneBM12", "allowUseItem", "(LAsh;II)I"),
                ("SceneBM12", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"))
    selected += tuple((owner, name, signature) for owner in
                      ("SceneBT02", "SceneBT04", "SceneBT06", "SceneBT08", "SceneBT10")
                      for name, signature in (("activateScene", "(Z)V"),
                                              ("allowUse", "(LAsh;I)I"),
                                              ("reportPickupItem", "(Led/base/EDNpc;I)V"),
                                              ("processFrame", "()V")))
    selected += (("SceneBT02", "allowUseItem", "(LAsh;II)I"),
                 ("SceneBT02", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"),
                 ("SceneBT04", "spawnMonster1", "(LGenericNpc;III)V"),
                 ("SceneBT08", "spawnMonster1", "(LGenericNpc;III)V"),
                 ("SceneBT06", "spawnDeathMonster1", "(LGenericNpc;III)V"),
                 ("SceneBT10", "spawnDeathMonster1", "(LGenericNpc;III)V"),
                 ("SceneBT11", "activateScene", "(Z)V"),
                 ("SceneBT11", "spawnMonster1", "(LGenericNpc;III)V"),
                 ("SceneBT11", "spawnWaveActivateMonster1", "(II)V"),
                 ("SceneBT11", "spawnDeathMonster1", "(LGenericNpc;III)V"),
                 ("SceneBT11", "processFrame", "()V"))
    selected += tuple((owner, name, signature) for owner in ("SceneBA01", "SceneBA02", "SceneBA11")
                      for name, signature in (("activateScene", "(Z)V"),
                                              ("allowUse", "(LAsh;I)I"),
                                              ("processFrame", "()V")))
    selected += (("SceneBA02", "spawnDeathMonster1", "(LGenericNpc;III)V"),
                 ("SceneBA02", "allowUseItem", "(LAsh;II)I"),
                 ("SceneBA02", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"),
                 ("SceneBG13", "activateScene", "(Z)V"),
                 ("SceneBG13", "reportPickupItem", "(Led/base/EDNpc;I)V"),
                 ("SceneBG13", "processFrame", "()V"))
    selected += tuple((owner, name, signature) for owner in
                      ("SceneBG02", "SceneBG03", "SceneBG06", "SceneBG07", "SceneBG08", "SceneBG10")
                      for name, signature in (("activateScene", "(Z)V"),
                                              ("allowUse", "(LAsh;I)I"),
                                              ("processFrame", "()V")))
    selected += tuple((owner, name, signature) for owner, name, signature in (
        ("SceneBG02", "allowUseItem", "(LAsh;II)I"),
        ("SceneBG02", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"),
        ("SceneBG03", "spawnMonster3", "(LGenericNpc;III)V"),
        ("SceneBG03", "spawnWaveActivateMonster3", "(II)V"),
        ("SceneBG06", "allowUseItem", "(LAsh;II)I"),
        ("SceneBG06", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"),
        ("SceneBG07", "allowUseItem", "(LAsh;II)I"),
        ("SceneBG07", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V"),
        ("SceneBG08", "spawnMonster1", "(LGenericNpc;III)V"),
        ("SceneBG08", "reportPickupItem", "(Led/base/EDNpc;I)V"),
        ("SceneBG10", "spawnMonster2", "(LGenericNpc;III)V")))
    selected += (("ArmoryBoss", "initialize", "()V"),
                 ("ArmoryBoss", "eventHitWeapon", "(III)V"),
                 ("ArmoryBoss", "actionAttack", "()V"),
                 ("SceneBG12", "hurtBossMolten", "()V"),
                 ("SceneBG12", "hurtBossSteam", "()V"),
                 ("SceneBG12", "allowUse", "(LAsh;I)I"),
                 ("SceneBG12", "processFrame", "()V"))
    methods = []
    for owner, name, signature in selected:
        index = vm.method_index(owner, name, signature)
        method = package.methods[index]
        offset, size = 56 + method["code_offset"], method["code_length"]
        methods.append({"index": index, "owner": owner, "name": name, "signature": signature,
                        "file_offset": offset, "bytes": size,
                        "sha256": hashlib.sha256(raw[offset:offset + size]).hexdigest()})
    def cases(owner, name, signature):
        return [case for row in package.disassemble(vm.method_index(owner, name, signature))
                for case in row.get("cases", [])]
    assert cases("SceneBM01", "allowUseItem", "(LAsh;II)I") == [[157, 20]]
    assert cases("SceneBM01", "reportPickupItem", "(Led/base/EDNpc;I)V") == [[20, 52], [21, 57], [22, 62], [112, 67], [169, 72]]
    assert cases("PoopCommander", "actionPoopPuzzle", "(LGenericNpc;)Z")[:3] == [[36, 52], [37, 58], [38, 64]]
    assert cases("SceneBT02", "allowUseItem", "(LAsh;II)I") == [[82, 104], [143, 44], [144, 74], [145, 119]]
    assert cases("SceneBT02", "reportUseItem", "(Led/base/EDNpc;Led/base/EDInventoryItem;)V") == [[82, 58], [143, 48], [144, 53], [145, 83]]
    wizard_frame = {row["offset"]: row for row in package.disassemble(vm.method_index("SceneBT11", "processFrame", "()V"))}
    assert wizard_frame[21]["field_index"] == 609
    assert wizard_frame[33]["constant"]["value_raw"] == 0x62610100  # BA01, not the BT01 portal fallback.
    assert wizard_frame[35]["mnemonic"] == "iconst_0" and wizard_frame[36]["mnemonic"] == "iconst_1"
    assert wizard_frame[37]["operands"] == [160] and wizard_frame[40]["method"]["name"] == "sceneSwitch"
    mechanism = package.disassemble(vm.method_index("SceneBA01", "allowUse", "(LAsh;I)I"))
    facing = next(index for index, row in enumerate(mechanism) if row["offset"] == 13)
    assert mechanism[facing]["method"]["name"] == "facingMarker"
    assert [_integer(mechanism[facing-2]), _integer(mechanism[facing-1])] == [3, 55]
    pump = {row["offset"]: row for row in package.disassemble(vm.method_index("SceneBA02", "allowUse", "(LAsh;I)I"))}
    assert pump[113]["field_index"] == 614 and pump[120]["field_index"] == 563
    assert pump[56]["constant"]["value_raw"] == 0x62610400  # BA04 after the water-control item.
    gear_report = {row["offset"]: row for row in package.disassemble(vm.method_index("SceneBA02", "reportUseItem"))}
    assert gear_report[25]["field_index"] == 566
    armory_frame = {row["offset"]: row for row in package.disassemble(vm.method_index("SceneBG12", "processFrame", "()V"))}
    assert armory_frame[22]["field_index"] == 610
    assert armory_frame[38]["constant"]["value_raw"] == 0x62670D00  # BG13 directly.
    assert armory_frame[41]["operands"] == [9] and armory_frame[43]["mnemonic"] == "iconst_1"
    assert armory_frame[44]["mnemonic"] == "iconst_0" and armory_frame[45]["method"]["name"] == "sceneSwitch"
    armory_hit = {row["offset"]: row for row in package.disassemble(vm.method_index("ArmoryBoss", "eventHitWeapon", "(III)V"))}
    assert armory_hit[7]["field_index"] == 1461 and armory_hit[19]["field_index"] == 1462
    assert armory_hit[27]["field_index"] == 13 and armory_hit[30]["operands"] == [50]
    assert armory_hit[32]["mnemonic"] == "isub" and armory_hit[33]["field_index"] == 13
    recipes_code = {row["offset"]: row for row in package.disassemble(vm.method_index("Ash", "inventoryCombine"))}
    assert recipes_code[1137]["field_index"] == 309 and recipes_code[1140]["constant"]["value_raw"] == 0x62670600
    assert recipes_code[1182]["field_index"] == 309 and recipes_code[1185]["constant"]["value_raw"] == 0x62670600
    ash = Object(vm.class_index("Ash"))
    item_class = vm.class_index("ed/base/EDInventoryItem")
    method = vm.method_index("Ash", "inventoryCombine", "(Led/base/EDInventoryItem;Led/base/EDInventoryItem;)I")
    recipes = []
    for first, second, expected in ((20, 21, 154), (21, 20, 154), (20, 22, 155), (21, 22, 156),
                                    (154, 22, 157), (22, 154, 157), (155, 21, 157), (156, 20, 157), (20, 20, -1),
                                    (94, 95, 180), (95, 94, 180), (180, 141, 18), (147, 148, -1), (148, 147, -1)):
        actual = vm.invoke(method, ash, (Object(item_class, {33: first}), Object(item_class, {33: second})))
        assert actual == expected
        recipes.append({"first": first, "second": second, "result": actual})
    return {"format": "EARNED-LEVELB-STORY1", "original_executable_run": False,
            "script_sha256": hashlib.sha256(raw).hexdigest(), "methods": methods, "recipe_oracle": recipes,
            "bridge": {"intro": "Actor enters original volume6; processFrame1956 starts source wave3/camera6, holds actor64, releases after source120-count sequence and sets526.",
                       "drops": "Original SceneBM01 wave callbacks assign skeleton flavors36/37/38. PoopCommander890 maps them to20(handle),21(axle),22(spool);891 invokes real SpawnCommander.spawnByNpc. Player damage/AI/drop creation must remain original.",
                       "pickup_reports": {"20": 527, "21": 528, "22": 529},
                       "assembly": "Actual Inventory.combineItemSlots -> InventoryItem -> Ash675:20+21=154,154+22=157. No recipe quest flag is required for these branches.",
                       "use": "Only157 at actorInTrigger3 && facingMarker(9,70) returns116 and disables volume2. Real reportUseItem1955 sets531 and requests game mode0; Interface applies844 later. Consumable157 is removed by original inventory code.",
                       "camera_regions": "After531, original processFrame1956 disables type0 camera regions41/42 and enables48/49. reportCameraState1952 then selects authored frame1 for tracks0/1. These are not floor polygons; type2 volume2 is the physical blocker disabled by allowUseItem157.",
                       "popup": "Each real pickup may open original mode2. Fixtures must acknowledge every popup (including incidental pickups), then wait applied844==0. This is user Back/ready behavior, never an AI pause override."},
            "next_source_frontier": {"bm02": "Actual spawnDeathMonster1 sets532; allowUse requires532 +volume7 +facingMarker(1,50), plays original movie with flags4, then enables portal1 toBM12.",
                                     "bm11": "Original processFrame entering volume67 sets539 and plays source conversation. allowUse volume68/facingMarker(43,65) grants141 only after539 and before533. Real reportPickupItem141 sets533. Later carpart94 requires both533 and541.",
                                     "bt01": "Original BM11 lever volume69/facing44 earns536; volume66/facing45 opens portal5 toBT01. Source object95 at marker21 reports541. Original BT01 volume16/facing14 reopens return portal5; no direct scene loads.",
                                     "bomb_gate": "After both533 and541, original BM11 interaction grants94 and reports537. Recipes94+95=180 and180+141=18 produce the bomb. BM12 volume7/facingMarker(2,60) accepts18, reports540, consumes18, selects authored camera frame, plays movie and opens portal2 toBT02."},
            "temple_source": {
                "bt04": "BT02 volume41/facing24 opens portal5. Original BT04 intro volume1 sets554 and starts GuildMember23 flavor39. Original puzzle-poop branch drops144; pickup report sets552. Return uses volume16/facing3/portal1.",
                "bt10": "Use144 at BT02 volume44/facing27 sets543; original door interaction opens portal8. BT10 intro volume1 sets558 and starts two GuildMember23. Authored object145 at marker1 reports550; return uses volume10/facing2/portal1.",
                "bt08": "BT02 volume43/facing26 opens portal7. Original BT08 intro volume0 sets555 and starts GuildMember23 flavor40. Puzzle-poop drops143; pickup report sets553. Return uses volume17/facing4/portal1.",
                "bt06": "Use143 at BT02 volume42/facing25 sets544; original interaction opens portal6. BT06 intro volume12 sets557 and starts two GuildMember23. Authored object82 reports551; return uses volume8/facing1/portal1.",
                "statues": "Original BT02 allowUseItem2005 accepts145 in volume46 (action117) and82 in volume47 (action115), with no facing predicate in these two branches. reportUseItem2011 sets545/547 and swaps the original statue objects. processFrame2012@22 checks both flags, then@40 requests sceneSwitch(0x62740B00,1,0,0), the real BT11 entry. Key use143/144 is action116 and requests game mode0.",
                "world_proof": "Source predicates alone do not prove earned reachability; the independent actual-World continuation must earn every item and drain source sceneSwitch requests outside the VM."},
            "wizard_source": "BT11 spawnWaveActivateMonster1 selects native26 WizardBoss at marker4, maxConcurrent/maxCumulative1. Original spawnDeathMonster1 clears401 and sets609. processFrame2053@21 tests609, plays the original ending movie with flags2, then@40 queues sceneSwitch(0x62610100,0,1,160): BA01 directly. The separate BT01 portal fallback is not this script transition.",
            "pump_source": "Original BA01 volume15/facingMarker(3,55) holds the player/camera for its source counter and movie; processFrame sets559. Volume13/facing6 then opens portal1 toBA02. Original BA02 trigger9/camera sequence sets560; free PumpKeeper39 death callback sets614. Only after614, volume11/facingMarker(5,50) sets563 and plays the original lever/movie/camera sequence. Volume10/facing7 opens source portal1 toBA11 while566 is false; when566 is true the source uses a direct BA04 sceneSwitch instead.",
            "gear_source": "BA11 volume25/facing8 sets562. Original BA11 volume26/facing10 opens portal4 toBT01; BT01 volume16/facing14 opens portal5 toBM11. Only after562, BM11 volume65/facing42 enables portal3 toBG02. BM11.activateScene1969@639 checks610: before ArmoryBoss death610 it disables portal4 toBG13 and enables3; after610 it enables4 and disables3. Thus the actual armory puzzle must be earned before BG13 object88. SceneBG12.processFrame1904 directly requests sceneSwitch(0x62670D00,9,1,0), BG13. That authored object88 at marker13 reports565; processFrame then enables return portal1. Back at BA02, real item88 use requires volume8/facingMarker(6,50), sets566 and changes the source camera. BA02 volume10/facing7 then requests BA04 directly; the separate BA11 portal3 is also gated by566 on activation. Literal source analysis alone does not establish this route's earned reachability.",
            "foundry_source": "BG07 authored146/report573 unlocks576 via BG02 volume19/facing18; the original BG08 Captain38 flavor43 drops150/report575. BG06 volume15/facing9 item150 use sets586 and transforms the item to149. BG07 volume14/facing6 item149 use sets583/582/577 and reveals authored148. Only after582, BG03.processFrame activates puzzle wave3, ArmorySkeleton28 flavor41 ->drop48; use48 atBG02 volume16/facing15 sets571/disables solid14. BG03 portal5 toBG10 requires577; the BG10 ArmorySkeleton28 flavor42 ->147/report580 is reached through its original internal same-scene doors. Ash.inventoryCombine675 only permits147+148=36 while currentSceneID309==BG06 and actorInTrigger13/facingMarker(7,40); outside that context the real method returns-1. Original BG07 item36 use in volume12 sets585, plays its movie and requests BG12. This is source dependency evidence; its independent actual-World pass is recorded in research/levelb-foundry-world-proof.json, with inherited-RNG coverage recorded separately.",
            "armory_source": "Original BG12 player volumes11(molten)/10(steam) run first movies1613/1612. Subsequent real Use checks the boss in volumes23/22; hurtBossMolten sets1461=1/1462=0, hurtBossSteam sets1461=1/1462=1 and sends source event5. Original ArmoryBoss.eventHitWeapon1695 subtracts50HP only in that pending steam branch, clears the pending state and runs source hit actions. Two cycles kill the weapon-immune boss; SceneBG12.processFrame1904 sets610 and directly queues BG13(0x62670D00,9,1,0). The natural-AI/actual-World proof is separate in research/levelb-armory-world-proof.json.",
            "world_proof": "runtime/levelb_story_world_tests.inl is a separate actual-World fixture; static oracle alone does not prove story reachability."}


if __name__ == "__main__":
    result = audit(ROOT / "game")
    (ROOT / "research/levelb-story-format.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Original LevelB: method hashes, bounded predicates and", len(result["recipe_oracle"]), "real JPC recipe cases passed")
