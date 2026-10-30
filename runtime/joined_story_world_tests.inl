#pragma once
#include "story_ending_world_tests.inl"
#include "levelb_ending_world_tests.inl"
// Keep the actual staged B World. Never create a fresh player/VM between chapters.
void continueJoinedLevelB(World& world,const std::function<void(World&)>& continuation={}) {
    continueLevelBStoryWorld(world,[&](World& b){
        continueLevelBMerchant(b);continueLevelBBomb(b);continueLevelBTemple(b);
        continueLevelBWizard(b);continueLevelBPump(b);continueLevelBFoundry(b);
        continueLevelBArmoryWorld(b);continueLevelBEnding(b);if(continuation)continuation(b);
    });
}
void testJoinedStoryWorld(const fs::path& root,const std::function<void(World&)>& continuation={}) {
    testEarnedEndingWorld(root,[&](World& b){continueJoinedLevelB(b,continuation);});
}
