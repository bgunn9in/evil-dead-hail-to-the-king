#include "actor_actions.h"
#include "source_playback_pool.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <tuple>
using namespace edactions;
struct Harness{
 SourcePlaybackPool pool;std::map<int,std::unique_ptr<ActorActions>> actors;
 std::vector<std::tuple<int,unsigned,unsigned>> events,observed;
 uint32_t random=12345;std::vector<std::tuple<int,unsigned,unsigned,uint32_t>> draws;
 std::vector<State> source;
 Harness(){source.resize(3);for(auto& s:source){s.animation=1;s.frames=10;s.end=s.loopEnd=1;s.finishStart=1;s.earlyEnd=1;}
  source[0].beginEvent=11;source[0].endEvent=12;source[0].transitions={{7,0,1},{8,1,2}};source[1].transitions={{39,2,0}};source[2].mode=1;source[2].beginEvent=11;}
 ActorActions& add(int owner){
  PoolHooks hook{[this,owner](bool priority){return pool.allocate({owner,0},priority);},
   [this,owner](unsigned n,const Playback& p){pool.changed({owner,0},n,p.active,p.reusable);},
   [this,owner](const Event& e){events.emplace_back(owner,e.playback,e.event);}};
  auto value=std::make_unique<ActorActions>(source,hook);auto& ref=*value;actors.emplace(owner,std::move(value));return ref;
 }
 void tick(){for(auto& [owner,a]:actors){(void)owner;a->beginTick();}
  for(unsigned n=0;n<50;++n){auto slot=pool.at(n);if(!slot.active)continue;
   actors.at(slot.owner.type)->tickSlot(n,[&](unsigned slot,const Playback& p){
    random=random*0x41c64e6du+0x3039u;const auto value=uint32_t(((uint64_t(random)*0x88888889u)>>35)%0x07ffffffu);
    observed.emplace_back(slot,unsigned(pool.at(slot).owner.type),unsigned(p.frame));draws.emplace_back(slot,unsigned(pool.at(slot).owner.type),unsigned(p.frame),value);});}}
 void complete(){for(unsigned n=0;n<50;++n){auto slot=pool.at(n);if(!slot.active&&!slot.reusable)actors.at(slot.owner.type)->completeSlot(n);}}
};
int main(){
 try{
  SourcePlaybackPool full;for(int n=0;n<50;++n)assert(full.allocate({n,0},false)==unsigned(n));const auto saved=full.state();bool bad=false;
  try{full.allocate({99,0},false);}catch(const std::runtime_error&){bad=true;}assert(bad);for(unsigned n=0;n<50;++n)assert(full.at(n).owner==saved[n].owner);
  SourcePlaybackPool high;for(int n=0;n<25;++n)assert(high.allocate({1,0},true)==unsigned(n+25));bad=false;
  try{high.allocate({1,0},true);}catch(const std::runtime_error&){bad=true;}assert(bad&&high.at(0).owner.empty());
  SourcePlaybackPool priority;assert(priority.allocate({1,0},true)==25);assert(priority.allocate({2,0},true)==26);
  priority.changed({1,0},25,false,true);assert(priority.allocate({3,0},true)==25);assert(priority.allocate({2,0},true)==27);
  Harness h;auto& a=h.add(1);auto& b=h.add(2);assert(a.layers[0].playback==0&&b.layers[0].playback==1);
  assert(a.command(8));assert(a.layers[6].playback==25);assert(b.command(7));assert(b.layers[0].playback==2);
  // Frame0 cross-owner ordering0(A),1(B-parent),2(B-child),25(Aoverlay).
  h.tick();assert((h.observed==std::vector<std::tuple<int,unsigned,unsigned>>{{0,1,0},{1,2,1},{2,2,0},{25,1,0}}));
  // Independent tools/ed_playback_pool.py oracle + native418f20 LCG values.
  assert((h.draws==std::vector<std::tuple<int,unsigned,unsigned,uint32_t>>{{0,1,0,102743356},{1,2,1,52586767},{2,2,0,105578977},{25,1,0,15285571}}));
  assert((h.events==std::vector<std::tuple<int,unsigned,unsigned>>{{1,0,11},{2,1,11},{1,25,11},{2,1,12}}));
  // Leaf2 is protected across actor/objectphysics, unavailable to allocation.
  h.tick();assert(!h.pool.at(2).active&&!h.pool.at(2).reusable);const auto allocated=h.pool.allocate({3,0},false);assert(allocated!=2);
  h.pool.changed({3,0},allocated,false,true);h.complete();assert(h.pool.at(2).reusable&&b.layers[0].playback==1&&h.pool.at(1).active); //39 mayreuse an earlier free slot.
  //4058e0 overwrites a waiting child without clearing parent+04. The
  // still-active parent retains child+28, so both links must survive a save.
  Harness overwritten;auto& waiting=overwritten.add(7);waiting.states[1].transitions={{9,2,2}};
  assert(waiting.command(7));const int child=waiting.layers[0].playback,parent=waiting.playbacks[size_t(child)].parent;
  assert(parent>=0&&waiting.command(9));assert(waiting.playbacks[size_t(child)].parent==parent&&waiting.playbacks[size_t(parent)].child==child);
  overwritten.tick();assert(waiting.playbacks[size_t(child)].active);
  // Native4187e6 must ignore an inactive layer's stale pointer to reused25.
  Harness reused;reused.source[1].endEvent=31;reused.source[1].transitions={{11,6,2}};reused.source[2].transitions={{10,0,0xfffe}};
  auto& reusedActor=reused.add(1);for(int n=0;n<24;++n)reused.add(n+10);
  assert(reusedActor.command(8)&&reusedActor.layers[6].playback==25);reusedActor.command(10);
  assert(reusedActor.layers[6].state==-1&&!reusedActor.playbacks[25].active);
  assert(reusedActor.command(7)&&reusedActor.layers[0].playback==25&&reusedActor.playbacks[25].parent==0);
  assert(reusedActor.command(11)&&reusedActor.layers[0].playback==25&&reusedActor.playbacks[25].parent==0);
  assert(std::get<2>(reused.events[reused.events.size()-2])==31); // Existing active overwrite emits oldend.
  // Later-parentРІвЂ вЂ™earlier-child waits untilnextpass; parentpriority25 childnormal1.
  Harness early;auto& c=early.add(4);assert(c.command(8));c.states[2].transitions={{9,0,1}};assert(c.command(9));
  assert(c.layers[6].playback==1);early.tick();assert(c.playbacks[1].frame==0&&c.playbacks[1].mode==0);
  early.observed.clear();early.tick();assert(std::get<0>(early.observed.front())==0&&std::get<0>(early.observed[1])==1);
  SourcePlaybackPool inactive;const auto old=inactive.allocate({8,0},false);inactive.changed({8,0},old,false,true);
  assert(inactive.allocate({9,0},false)==old);inactive.changed({8,0},old,false,true);
  assert((inactive.at(old).owner==PlaybackOwner{9,0})&&inactive.at(old).active); // stale inactive references cannot free foreignlive slots.
  inactive.clearExceptType(9);assert(inactive.at(old).active);inactive.clearExceptType(0);assert(inactive.at(old).owner.empty());
  // live sweep39 can disable a later pending record, no captured completionlist.
  Harness live;auto& d=live.add(5);auto& e=live.add(6);d.playbacks[0].mode=5;e.playbacks[1].mode=5;live.tick();
  d.states[0].transitions={{39,1,2}};d.completeSlot(0);assert(live.pool.at(0).reusable&&live.pool.at(25).active);
  live.pool.changed({6,0},1,false,true);e.playbacks[1].reusable=true;live.complete();assert(e.layers[6].state==-1);
  std::cout<<"Global50 allocator, actor-specificpriority, cross-ownerpreStep/childcadence, physicsreservation andLIVE39: passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
