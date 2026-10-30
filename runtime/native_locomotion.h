#pragma once
#include "actor_actions.h"

namespace edlocomotion {
inline const edactions::Layer& rootLayer(const edactions::ActorActions& actions) {
    const auto* owner=&actions.layers[0];
    // Queued commands follow their logical layer; root playback may still have a mode4 parent.
    for(unsigned layer=1;layer<7;++layer) {
        const auto& candidate=actions.layers[layer];
        if(candidate.state>=0&&candidate.playback>=0) {
            const auto& mask=actions.states.at(size_t(candidate.state)).boneMask;
            if(!mask.empty()&&mask[0]){owner=&candidate;break;}
        }
    }
    return *owner;
}
inline int manualAction(const edactions::ActorActions& actions) {
    const auto* owner=&rootLayer(actions);
    if(owner->state<0||owner->playback<0||size_t(owner->playback)>=actions.playbacks.size())return 0;
    const auto& state=actions.states.at(size_t(owner->state));
    if(state.mode!=1)return 0;
    if(state.animation==actions.states.at(11).animation)return 3;
    return state.animation==actions.states.at(12).animation?145:0;
}
inline bool backwardActive(const edactions::ActorActions& actions) {
    const auto& owner=rootLayer(actions);
    if(owner.state>=0&&actions.states.at(size_t(owner.state)).animation==actions.states.at(13).animation)return true;
    int index=owner.playback;if(index<0)return false;
    if(actions.playbacks.at(size_t(index)).mode==4&&actions.playbacks.at(size_t(index)).parent>=0)index=actions.playbacks.at(size_t(index)).parent;
    const auto& playback=actions.playbacks.at(size_t(index));
    return playback.active&&actions.states.at(size_t(playback.state)).animation==actions.states.at(13).animation;
}
}
