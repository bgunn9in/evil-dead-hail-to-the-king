#include "actor_actions.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static void playbackDispatchOrder() {
    std::vector<edactions::State> states(2);
    states[0].animation=1;states[0].frames=3;states[0].endEvent=31;
    states[0].transitions.push_back({7,0,1});
    states[1].animation=2;states[1].frames=3;states[1].end=states[1].loopEnd=2;
    const auto make=[&] {edactions::ActorActions actor(states);assert(actor.command(7));return actor;};
    auto later=make();std::vector<std::array<unsigned,3>> order;
    later.tick([&](unsigned index,const edactions::Playback& play){
        order.push_back({index,unsigned(play.state),unsigned(play.frame)});
        if(index==1)assert(!later.playbacks[0].active&&later.events.size()==1&&later.events[0].event==31&&later.events[0].update==1);
    });
    assert((order==std::vector<std::array<unsigned,3>>{{0,0,0},{1,1,0}}));
    assert(later.playbacks[1].frame==1&&later.updates==1);
    // A mode4 child must not emit its initial frame until its parent finishes.
    auto waiting=make();waiting.states[0].end=1;order.clear();
    waiting.tick([&](unsigned index,const auto& play){order.push_back({index,unsigned(play.state),unsigned(play.frame)});});
    assert((order==std::vector<std::array<unsigned,3>>{{0,0,0}})&&waiting.playbacks[1].mode==4&&waiting.playbacks[1].frame==0);
    order.clear();waiting.tick([&](unsigned index,const auto& play){order.push_back({index,unsigned(play.state),unsigned(play.frame)});});
    assert((order==std::vector<std::array<unsigned,3>>{{0,0,1},{1,1,0}}));
    // Reused lower pool slot: its turn has passed when the later parent ends.
    // The newly unblocked child's frame0 belongs to the following native pass.
    auto earlier=make();std::swap(earlier.playbacks[0],earlier.playbacks[1]);
    earlier.playbacks[0].parent=1;earlier.playbacks[1].child=0;earlier.layers[0].playback=0;order.clear();
    earlier.tick([&](unsigned index,const auto& play){order.push_back({index,unsigned(play.state),unsigned(play.frame)});});
    assert((order==std::vector<std::array<unsigned,3>>{{1,0,0}})&&earlier.playbacks[0].mode==0&&earlier.playbacks[0].frame==0);
    order.clear();earlier.tick([&](unsigned index,const auto& play){order.push_back({index,unsigned(play.state),unsigned(play.frame)});});
    assert((order==std::vector<std::array<unsigned,3>>{{0,1,0}})&&earlier.playbacks[0].frame==1);
}

static void weaponLocomotion(const std::vector<edactions::State>& source) {
    std::vector<unsigned> classified;
    for(unsigned i=0;i<source.size();++i) {
        const auto& state=source[i];
        if(state.mode==1&&state.boneMask==source[11].boneMask&&
           (state.animation==source[11].animation||state.animation==source[12].animation))classified.push_back(i);
    }
    assert((classified==std::vector<unsigned>{11,12,39,40,67,68,95,96}));
    // Original equip actions select default/shotgun/gun/rifle families. The
    // transitions, rather than a forced idle index, own every stop and restart.
    for(const auto& family:std::vector<std::array<int,4>>{{0,0,11,12},{85,28,39,40},{82,56,67,68},{83,84,95,96}}) {
        edactions::ActorActions actor(source);
        if(family[0])assert(actor.command(family[0]));
        for(unsigned frame=0;frame<60;++frame)actor.tick();
        assert(actor.layers[0].state==family[1]);
        for(const auto& movement:std::vector<std::array<int,3>>{{3,4,family[2]},{145,4,family[3]},{3,4,family[2]}}) {
            assert(actor.possible(movement[0])&&actor.command(movement[0]));
            for(unsigned frame=0;frame<60;++frame)actor.tick();
            assert(actor.layers[0].state==movement[2]);
            assert(source[size_t(movement[2])].mode==1&&source[size_t(movement[2])].boneMask==source[11].boneMask);
            for(unsigned frame=0;frame<3;++frame){actor.tick();assert(actor.layers[0].state==movement[2]);}
            assert(!actor.possible(3)&&!actor.possible(145));
            assert(!actor.command(movement[0])&&actor.layers[0].state==movement[2]);
            assert(actor.possible(movement[1])&&actor.command(movement[1])&&actor.layers[0].state==family[1]);
            for(unsigned frame=0;frame<60;++frame)actor.tick();
            assert(actor.layers[0].state==family[1]);
        }
    }
    // Root/leg-only movement overlays have stack-return/disable stops. They
    // must not become full-body locomotion or be replaced by a guessed idle.
    for(unsigned i=153;i<=161;++i) {
        const auto& state=source[i];assert(state.mode==1&&state.boneMask!=source[11].boneMask);
        const auto stop=std::find_if(state.transitions.begin(),state.transitions.end(),[](const auto& rule){return rule.action==4||rule.action==6;});
        assert(stop!=state.transitions.end()&&stop->target==(i<158?0xffff:0xfffe));
    }
}

int main(int argc,char** argv) {
    try {
        playbackDispatchOrder();
        if(argc!=2)throw std::runtime_error("Pass the exported original actions.bin");const auto source=edactions::load(argv[1]);assert(source.size()==163);
        assert(source[0].animation==0x01024084&&source[11].animation==0x01824284);
        weaponLocomotion(source);
        for(const auto& test:std::vector<std::array<int,4>>{{115,146,35,34},{116,147,29,34},{117,148,17,34}}) {
            edactions::ActorActions actor(source);assert(actor.possible(test[0]));assert(actor.command(test[0]));assert(actor.layers[0].state==test[1]);
            assert(edactions::curveFrame(source[size_t(test[1])],actor.playbacks[size_t(actor.layers[0].playback)])==source[size_t(test[1])].frames-1u);
            assert(!actor.possible(3)&&!actor.possible(5));
            for(int frame=1;frame<test[2];++frame) {actor.tick();assert(actor.events.empty());}
            actor.tick();assert(actor.events.size()==1&&actor.events[0].event==test[3]&&actor.events[0].update==uint64_t(test[2]));assert(actor.layers[0].state==0);
        }
        for(const auto& test:std::vector<std::array<int,3>>{{32,130,12},{33,131,12},{34,132,15},{35,133,12},{37,134,15}}) {
            edactions::ActorActions actor(source);assert(actor.command(test[0]));assert(actor.layers[6].state==test[1]);
            for(int frame=1;frame<test[2];++frame){actor.tick();assert(actor.events.empty());}actor.tick();
            assert(actor.events.size()==1&&actor.events[0].event==26&&actor.layers[6].state<0&&actor.layers[0].state==0);
        }
        edactions::ActorActions actor(source);assert(actor.command(3));for(int frame=0;frame<100;++frame)actor.tick();
        const auto& walk=actor.playbacks[size_t(actor.layers[0].playback)];assert(walk.frame>=source[11].loopStart&&walk.frame<=source[11].loopEnd);assert(actor.events.empty());
        assert(actor.command(4));assert(actor.layers[0].state==0);assert(actor.command(116));for(int frame=0;frame<60;++frame)actor.tick();
        assert(actor.layers[0].state==0&&actor.events.size()==1&&actor.events[0].event==34);
        edactions::ActorActions backward(source);assert(backward.command(5)&&backward.layers[0].state==13);
        for(unsigned i=0;i<400;++i)backward.tick();
        assert(!backward.possible(4)&&backward.possible(6)&&!backward.command(4)&&backward.layers[0].state==13);
        assert(backward.command(6)&&backward.layers[0].state==0&&backward.possible(116)&&backward.possible(3));
        edactions::ActorActions running(source);assert(running.command(145)&&running.layers[0].state==12);
        for(unsigned i=0;i<400;++i)running.tick();
        assert(!running.possible(6)&&running.possible(4)&&!running.possible(146));
        assert(running.command(25)&&running.layers[0].state==11&&running.command(24)&&running.layers[0].state==12);
        assert(running.command(4)&&running.layers[0].state==0);
        edactions::ActorActions death(source);assert(death.command(35));assert(death.command(64));assert(death.layers[0].state==162&&death.layers[6].state<0);
        for(int frame=0;frame<300;++frame)death.tick();assert(death.layers[0].state==162);assert(!death.possible(3)&&!death.possible(116));
        assert(death.playbacks[size_t(death.layers[0].playback)].frame==138);
        const auto before=death.layers[0].state;assert(!death.command(255)&&death.layers[0].state==before);
        std::cout<<"Original163-state actions/7layers/50playbacks/use/hit/death: passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
