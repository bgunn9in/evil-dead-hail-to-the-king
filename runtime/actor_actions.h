#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

namespace edactions {
struct Transition {uint8_t action=0,flags=0;uint16_t target=0;};
struct State {
    uint32_t animation=0,mode=0,clipFlags=0;
    int16_t callback=0;uint8_t beginEvent=0,endEvent=0;
    uint16_t start=0,end=0,loopStart=0,loopEnd=0,earlyEnd=0,finishStart=0,frames=0,bones=0;
    std::vector<uint8_t> boneMask;
    std::vector<Transition> transitions;
};
inline std::vector<State> load(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open original actor actions");
    const auto length=input.tellg();if(length<12||uint64_t(length)>1024*1024)throw std::runtime_error("Invalid actor action asset length");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Truncated actor actions");
    size_t cursor=0;
    auto u8=[&]() {if(cursor>=bytes.size())throw std::runtime_error("Actor action record outside asset");return bytes[cursor++];};
    auto u16=[&]() {const uint16_t a=u8();return uint16_t(a|(uint16_t(u8())<<8));};
    auto u32=[&]() {const uint32_t a=u16();return a|(uint32_t(u16())<<16);};
    const char magic[8]={'E','D','A','C','T','1',0,0};for(char c:magic)if(u8()!=uint8_t(c))throw std::runtime_error("Invalid actor actions magic");
    const auto count=u32();if(!count||count>4096)throw std::runtime_error("Excessive actor action state count");
    std::vector<State> states;
    for(uint32_t n=0;n<count;++n) {
        State s;s.animation=u32();s.mode=u32();const uint16_t callback=u16();
        if(callback>10)throw std::runtime_error("Unsupported native action callback");s.callback=int16_t(callback);
        s.beginEvent=u8();s.endEvent=u8();s.clipFlags=u32();
        s.start=u16();s.end=u16();s.loopStart=u16();s.loopEnd=u16();s.earlyEnd=u16();s.finishStart=u16();s.frames=u16();s.bones=u16();
        const auto mask=u32(),rules=u32();
        if(s.mode>1||s.bones>256||mask>s.bones||rules>256||
            (s.animation&&(!s.frames||s.start>=s.frames||s.end>=s.frames||s.loopStart>=s.frames||
                s.loopEnd>=s.frames||s.earlyEnd>=s.frames||s.finishStart>=s.frames)))throw std::runtime_error("Invalid original action timeline");
        for(uint32_t i=0;i<mask;++i)s.boneMask.push_back(u8());
        for(uint32_t i=0;i<rules;++i) {
            Transition t;t.action=u8();t.flags=u8();t.target=u16();
            if((t.flags&~7u)||(t.target>=count&&t.target<0xfffeu))throw std::runtime_error("Invalid actor action transition");
            s.transitions.push_back(t);
        }
        states.push_back(std::move(s));
    }
    if(cursor!=bytes.size())throw std::runtime_error("Trailing actor action data");return states;
}
struct Layer {
    int state=-1,playback=-1,historyIndex=0;
    std::array<int,10> history{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
};
struct Playback {
    int state=0,frame=0,mode=0,desiredMode=0,step=1,parent=-1,child=-1;
    bool active=false,reusable=true;
};
inline unsigned curveFrame(const State& state,const Playback& playback) {
    if(playback.frame<0||unsigned(playback.frame)>=state.frames)throw std::runtime_error("Animation curve frame outside clip");
    const unsigned frame=unsigned(playback.frame);
    return (state.clipFlags&2)?(frame?frame-1:state.frames-1):frame;
}
struct Event {uint64_t update=0;uint8_t event=0;unsigned playback=0,state=0;};
struct PoolHooks {
    std::function<unsigned(bool)> allocate;
    std::function<void(unsigned,const Playback&)> changed;
    std::function<void(const Event&)> event;
    std::function<void(unsigned)> activateChild;
};
class ActorActions {
    PoolHooks hooks;
    void changed(unsigned index){if(hooks.changed)hooks.changed(index,playbacks.at(index));}
    void emit(unsigned playback,uint8_t event,unsigned state) {if(event){const Event value{updates,event,playback,state};if(hooks.event)hooks.event(value);else events.push_back(value);}}
    void configure(unsigned index,int state,int mode,int parent=-1) {
        const auto& s=states.at(size_t(state));Playback p;
        // shortcut: Normal allocations reset the native stale desired-mode byte; revisit if a normal-target alias affects gameplay.
        p.state=state;p.parent=parent;p.frame=mode==2?s.loopStart:s.start;p.mode=mode;p.desiredMode=mode;p.active=true;p.reusable=bool(hooks.allocate);
        playbacks.at(index)=p;changed(index);emit(index,s.beginEvent,unsigned(state));
    }
    int create(int state,int mode,bool priority) {
        if(!states.at(size_t(state)).animation)return -1;
        if(hooks.allocate){const unsigned index=hooks.allocate(priority);configure(index,state,mode);return int(index);}
        unsigned first=0;
        if(priority) {first=25;for(unsigned i=25;i<50;++i)if(playbacks[i].active)first=i+1;}
        for(unsigned i=first;i<50;++i)if(!playbacks[i].active&&playbacks[i].reusable) {configure(i,state,mode);return int(i);}
        throw std::runtime_error("Original animation pool is full");
    }
    void overwrite(unsigned index,int state,int mode) {
        const auto previous=playbacks.at(index);if(previous.active)emit(index,states.at(size_t(previous.state)).endEvent,unsigned(previous.state));
        //4058e0 resets child+28 but deliberately preserves parent+04.
        configure(index,state,mode,previous.parent);
    }
    void replace(unsigned slot,int target,bool immediate) {
        auto& layer=layers.at(slot);const auto& s=states.at(size_t(target));
        if(layer.playback<0||!playbacks.at(size_t(layer.playback)).active)layer.playback=create(target,int(s.mode),false);
        else if(immediate)overwrite(unsigned(layer.playback),target,int(s.mode));
        else if(s.animation) {
            const int index=create(target,4,false);auto& old=playbacks.at(size_t(layer.playback));old.child=index;
            auto& child=playbacks.at(size_t(index));child.parent=layer.playback;child.desiredMode=int(s.mode);
            const auto& previous=states.at(size_t(old.state));
            if((previous.clipFlags&4)&&old.frame<=previous.earlyEnd)old.mode=6;
            else {old.frame=previous.finishStart;old.mode=3;old.step=previous.end>=old.frame?1:-1;}
            layer.playback=index;
        }
        layer.state=target;
    }
    void disable(Layer& layer) {
        layer.state=-1;if(layer.playback>=0) {auto& play=playbacks.at(size_t(layer.playback));play.active=false;play.reusable=true;changed(unsigned(layer.playback));}
    }
public:
    std::vector<State> states;
    std::array<Layer,7> layers;
    std::array<Playback,50> playbacks;
    std::vector<Event> events;
    uint64_t updates=0;
    explicit ActorActions(std::vector<State> source,PoolHooks binding={},bool initialize=true):hooks(std::move(binding)),states(std::move(source)) {
        if(states.empty())throw std::runtime_error("Actor action table is empty");if(initialize){layers[0].state=0;layers[0].playback=create(0,1,false);}
    }
    void bindPool(PoolHooks binding){hooks=std::move(binding);}
    void beginTick(){if(updates==std::numeric_limits<uint64_t>::max())throw std::runtime_error("Actor action update overflow");++updates;}
    bool possible(int action) const {
        for(const auto& layer:layers)if(layer.state>=0)for(const auto& rule:states.at(size_t(layer.state)).transitions)
            if(rule.action==action&&rule.target!=0xfffe) {
                if(!(rule.flags&1))return true;for(const auto& candidate:layers)if(candidate.state<0)return true;return false;
            }
        return false;
    }
    bool command(int action) {
        bool accepted=false;
        for(unsigned slot=0;slot<7;++slot) {
            auto& layer=layers[slot];if(layer.state<0)continue;
            const Transition* found=nullptr;
            for(const auto& rule:states.at(size_t(layer.state)).transitions)if(rule.action==action){found=&rule;break;}
            if(!found)continue;const auto rule=*found;const int target=rule.target;
            if(target==0xfffe) {disable(layer);continue;}
            accepted=true;
            if(target==0xffff) {
                const int previous=layer.history[size_t(layer.historyIndex)];layer.historyIndex=(layer.historyIndex+9)%10;
                if(previous<0) {
                    layer.state=-1;if(layer.playback>=0) {auto& p=playbacks.at(size_t(layer.playback));p.mode=(states.at(size_t(p.state)).clipFlags&4)?6:5;}
                } else {
                    layer.state=previous;
                    if(layer.playback>=0&&playbacks.at(size_t(layer.playback)).active)overwrite(unsigned(layer.playback),previous,2);
                    else layer.playback=create(previous,2,false);
                }
                continue;
            }
            //4187e6 skips disabled layers whose historical pointer may already be reused.
            if((rule.flags&4)&&!slot)for(unsigned i=1;i<7;++i)if(layers[i].state>=0)disable(layers[i]);
            bool duplicate=false;
            for(unsigned i=0;i<7;++i)if(layers[i].state==target&&((rule.flags&1)||i!=slot))duplicate=true;
            if(duplicate) {
                if(!(rule.flags&1)) {layer.state=-1;if(layer.playback>=0)playbacks.at(size_t(layer.playback)).mode=3;}
                continue;
            }
            if(rule.flags&1) {
                int free=-1;for(unsigned i=0;i<7;++i)if(layers[i].state<0)free=int(i);
                if(free<0)return false;auto& destination=layers[size_t(free)];destination.historyIndex=0;destination.history[0]=-1;
                destination.state=target;destination.playback=create(target,int(states.at(size_t(target)).mode),true);
            } else {
                layer.historyIndex=(layer.historyIndex+1)%10;layer.history[size_t(layer.historyIndex)]=layer.state;replace(slot,target,(rule.flags&2)!=0);
            }
        }
        return accepted;
    }
    using PreStep=std::function<void(unsigned,const Playback&)>;
    void tickSlot(unsigned index,const PreStep& preStep={}) {
        auto& play=playbacks.at(index);if(!play.active||play.mode==4){changed(index);return;}
        //40612f before40624c, in ascending GLOBAL slot order when pool-bound.
        if(preStep)preStep(index,play);
        const auto& s=states.at(size_t(play.state));play.frame+=play.step;
        if(play.mode==6&&play.frame>s.earlyEnd){play.frame=s.finishStart;play.mode=3;play.step=s.end>=play.frame?1:-1;}
        if(play.mode==1&&play.frame>=s.loopStart){play.mode=2;if(s.clipFlags&0x100)emit(index,s.endEvent,unsigned(play.state));}
        if(play.mode==2){if(play.frame>s.loopEnd)play.frame=s.loopStart;changed(index);return;}
        const int limit=(s.clipFlags&8)?s.earlyEnd:s.end;
        const bool finished=play.mode==5||(play.mode==0&&play.frame>limit)||
            (play.mode==3&&(play.step>0?play.frame>s.end:play.frame<s.end));
        if(finished){play.active=false;emit(index,s.endEvent,unsigned(play.state));
            if(play.child>=0){
                if(hooks.activateChild)hooks.activateChild(unsigned(play.child));
                else {auto& child=playbacks.at(size_t(play.child));
                    if(child.child>=0){child.mode=3;child.frame=states.at(size_t(child.state)).finishStart;}
                    else child.mode=child.desiredMode;
                    changed(unsigned(play.child));}
                play.reusable=true;
            }else play.reusable=false;
        }
        changed(index);
    }
    void completeSlot(unsigned index){
        auto& p=playbacks.at(index);if(p.active||p.reusable)return;
        p.reusable=true;changed(index);command(39); //406560 frees BEFORE direct39.
    }
    void tick(const PreStep& preStep={}) {
        beginTick();for(unsigned index=0;index<50;++index)tickSlot(index,preStep);
        for(unsigned index=0;index<50;++index)completeSlot(index);
    }
};
} // namespace edactions
