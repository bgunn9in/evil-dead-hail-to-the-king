#pragma once
#include <array>
#include <stdexcept>

namespace edactions {
struct PlaybackOwner {
    int type=-1,index=-1;
    bool operator==(const PlaybackOwner& b)const{return type==b.type&&index==b.index;}
    bool operator!=(const PlaybackOwner& b)const{return !(*this==b);}
    bool empty()const{return type<0;}
};
struct PlaybackSlot {PlaybackOwner owner;bool active=false,reusable=true;};
// PC405500/4057a0: one process-wide50-record allocation domain, independent
// of the physical13 actor registrations. Actual timelines remain in their owner.
class SourcePlaybackPool {
public:
    using State=std::array<PlaybackSlot,50>;
private:
    State slots{};
public:
    const State& state()const{return slots;}
    const PlaybackSlot& at(unsigned n)const{return slots.at(n);}
    unsigned allocate(PlaybackOwner owner,bool priority) {
        if(owner.empty()||owner.index<0)throw std::runtime_error("Invalid global playback owner");
        unsigned first=0;
        if(priority){first=25;for(unsigned n=25;n<50;++n)if(slots[n].active&&slots[n].owner==owner)first=n+1;}
        for(unsigned n=first;n<50;++n)if(!slots[n].active&&slots[n].reusable){slots[n]={owner,true,true};return n;}
        // compatibility repair: PC405818 writes slot50 into adjacent globals.
        // Reject before mutation instead of reproducing that memory corruption.
        throw std::runtime_error("Original global50 animation pool is full");
    }
    void changed(PlaybackOwner owner,unsigned n,bool active,bool reusable) {
        auto& slot=slots.at(n);
        if(slot.owner!=owner){
            // Inactive local layer/parent pointers may outlive slot reuse. They
            // neither claim ownership nor release another actor's live record.
            if(!active&&reusable)return;
            throw std::runtime_error("Stale global animation ownership");
        }
        slot.active=active;slot.reusable=reusable;
    }
    void clearExceptType(int type) {for(auto& s:slots)if(s.owner.type!=type)s={};}
    void restore(const State& state) {
        for(const auto& s:state)if((s.owner.empty()&&(s.owner.type!=-1||s.owner.index!=-1||s.active||!s.reusable))||(!s.owner.empty()&&s.owner.index<0))
            throw std::runtime_error("Invalid global playback pool state");
        slots=state;
    }
};
} // namespace edactions
