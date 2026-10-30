#pragma once
#include "source_animation_event.h"
#include "source_scene_queries.h"

// Native405CC0 sound-table dispatch,416D90 material query and4180C0 pitch ABI.
namespace edanimsound {
using Materials=std::array<uint8_t,4>;
inline bool verifiedActor(int type) {
    constexpr std::array<int,28> types{{0,2,4,5,6,7,8,9,10,11,12,13,14,17,18,19,23,24,26,27,28,29,31,33,34,35,38,39}};
    return std::find(types.begin(),types.end(),type)!=types.end();
}
inline Materials surfaceCodes(const SourceSceneQueries& scene,GroundPoint local,const std::vector<bool>& enabled) {
    if(enabled.size()!=scene.volumes.size())throw std::runtime_error("Animation material activation table differs from source");
    if(!std::isfinite(local.x)||!std::isfinite(local.y)||!std::isfinite(local.z))throw std::runtime_error("Non-finite animation material position");
    Materials result{};unsigned count=0;const auto mask=scene.pointMask(local);
    for(size_t i=0;i<scene.volumes.size();++i) {
        const auto& volume=scene.volumes[i];
        if(volume.geometry.kind!=7||!SourceSceneQueries::maskMatch(mask,volume.mask)||!volume.geometry.contains(local)||!enabled[i])continue;
        for(int shift:{24,16,8,0})if(const uint8_t code=uint8_t(volume.flags>>shift)) {
            result[count++]=code;if(count==result.size())return result;
        }
    }
    return result;
}
inline std::array<int32_t,11> ashArguments(const edanimevent::Sound& sound,int actorIndex,int resolvedClip,Materials material) {
    int32_t resource;std::memcpy(&resource,&sound.resource,4);
    return {{0,actorIndex,100,sound.frequencyHz()*2,resource,resolvedClip,
        material[0],material[1],material[2],material[3],0}};
}
inline int actorFrequencyHz(int scriptFrequency) {
    // SAR at4180DF rounds negative odd integers down.430AE9 uses WAV rate for<=0.
    const int64_t value=scriptFrequency;
    const int64_t half=value>=0?value/2:-((-value+1)/2);
    return half>0?int(half):0;
}
} // namespace edanimsound
