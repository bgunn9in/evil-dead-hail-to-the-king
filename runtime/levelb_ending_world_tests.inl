#pragma once
#include "levelb_armory_world_tests.inl"
#include "levelb_mud_world_tests.inl"
#include "levelb_library_world_tests.inl"
#include "levelb_badash_world_tests.inl"
#include "levelb_world_tests.inl"

void continueLevelBEnding(World& world) {
    if(world.scene.id!="ba02")throw std::runtime_error("Earned B ending requires the actual BA02 gear checkpoint");
    continueLevelBMud(world);continueLevelBLibrary(world);continueLevelBBadAsh(world);
    continueCthulhuWorld(world,false,640,480,{});
}
void testLevelBEndingWorld(const fs::path& root) {
    testLevelBArmoryWorld(root,[](World& world){continueLevelBEnding(world);});
}
