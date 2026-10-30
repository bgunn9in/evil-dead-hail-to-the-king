#include "actor_startup_fixture.h"

static void rejected(const std::function<void()>& f){bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}assert(failed);}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("Expected original exported A asset root");const std::filesystem::path root=argv[1];
 StartupCase* live=nullptr;unsigned physicsPhase=0;bool checking=false;int pendingSlot=-1;
 StartupCase source(root/"scripts/levelA.jpc",root,false,[&](edactors::Host& host){
  host.applyMovement=[&](edactors::Body& body){++physicsPhase;(void)body;if(checking){assert(live);const auto state=live->actors->cloneState();
   const auto& slot=state.playbackPool[size_t(pendingSlot)];assert(!slot.active&&!slot.reusable);}};
 });live=&source;auto& actors=*source.actors;auto& vm=source.game.vm;
 const auto originalVm=vm.snapshot(),original=actors.snapshot();const auto state=actors.cloneState();
 assert(state.globalPlayback&&state.bodies.size()==9);
 for(unsigned n=0;n<50;++n){const auto& slot=state.playbackPool[n];if(slot.owner.empty())continue;
  const auto& p=actors.at({slot.owner.type,slot.owner.index}).actions.playbacks[n];assert(p.active==slot.active&&p.reusable==slot.reusable);}
 actors.resetSceneRuntime();const auto reset=actors.snapshot();actors.restore(reset);assert(actors.snapshot()==reset);
 vm.restore(originalVm);actors.restore(original);
 auto bad=state;bad.playbackPool[0].owner={99,99};rejected([&]{actors.restore(bad);});assert(actors.snapshot()==original);
 bad=state;bad.playbackPool[49].owner={-2,-1};rejected([&]{actors.restore(bad);});assert(actors.snapshot()==original);
 bad=state;auto& b=bad.bodies.front();const auto slot=unsigned(b.layers[0].playback);
 // Forge reciprocal two-node childcycle; everyindex/owner isotherwisevalid.
 unsigned other=49;assert(bad.playbackPool[other].owner.empty());b.playbacks[other]=b.playbacks[slot];
 b.playbacks[slot].mode=4;b.playbacks[slot].parent=int(other);b.playbacks[slot].child=int(other);
 b.playbacks[other].mode=4;b.playbacks[other].parent=int(slot);b.playbacks[other].child=int(slot);
 bad.playbackPool[other]={{b.key.type,b.key.index},true,true};rejected([&]{actors.restore(bad);});assert(actors.snapshot()==original);
 // Actual AC05 first-pillar save retained Ash active9/11 -> inactive4 -> active12.
 //418717/4187f7 leave disabled child links, and4063f2 dereferences them without an active test.
 auto stale=state;auto& historical=stale.bodies.front();const auto parent=unsigned(historical.layers[0].playback);
 const unsigned inactive=49,descendant=48;assert(stale.playbackPool[inactive].owner.empty()&&stale.playbackPool[descendant].owner.empty());
 historical.playbacks[parent].child=int(inactive);
 historical.playbacks[inactive]=historical.playbacks[parent];auto& oldChild=historical.playbacks[inactive];
 oldChild.active=false;oldChild.parent=11;oldChild.child=int(descendant);
 historical.playbacks[descendant]=historical.playbacks[parent];auto& nextChild=historical.playbacks[descendant];
 nextChild.parent=int(inactive);nextChild.child=-1;
 stale.playbackPool[inactive]={{historical.key.type,historical.key.index},false,true};
 stale.playbackPool[descendant]={{historical.key.type,historical.key.index},true,true};
 actors.restore(stale);const auto legal=actors.snapshot();actors.restore(legal);assert(actors.snapshot()==legal);
 auto invalidStale=stale;invalidStale.bodies.front().playbacks[parent].child=50;
 rejected([&]{actors.restore(invalidStale);});assert(actors.snapshot()==legal);
 invalidStale=stale;invalidStale.playbackPool[inactive].owner={99,99};
 rejected([&]{actors.restore(invalidStale);});assert(actors.snapshot()==legal);
 // A playing reused slot may have a historical incoming pointer. A waiting slot,
 // however, must reciprocate its own live parent: parent11 here is not that parent.
 invalidStale=stale;invalidStale.bodies.front().playbacks[inactive].active=true;
 invalidStale.bodies.front().playbacks[inactive].mode=4;invalidStale.playbackPool[inactive].active=true;
 rejected([&]{actors.restore(invalidStale);});assert(actors.snapshot()==legal);
 invalidStale=stale;invalidStale.bodies.front().playbacks[inactive].child=int(parent);
 rejected([&]{actors.restore(invalidStale);});assert(actors.snapshot()==legal);
 actors.restore(original);
 // BM02 tick3045 retained active Ash10 -> global8, now waiting NPC28:0.
 // Reuse never scans older incoming pointers;4063f2 activates the current owner.
 auto foreign=state;auto& from=foreign.bodies.front();
 auto& to=*std::find_if(foreign.bodies.begin(),foreign.bodies.end(),[](const auto& item){return item.key.type==4;});
 const unsigned fromSlot=unsigned(from.layers[0].playback),toParent=unsigned(to.layers[0].playback),reused=49;
 from.flags=to.flags=1;
 auto& foreignParent=from.playbacks[fromSlot];const auto fromClip=actors.at(from.key).actions.states.at(size_t(foreignParent.state));
 foreignParent.mode=0;foreignParent.frame=(fromClip.clipFlags&8)?fromClip.earlyEnd:fromClip.end;foreignParent.step=1;foreignParent.child=int(reused);
 auto& remoteParent=to.playbacks[toParent];remoteParent.mode=2;remoteParent.child=int(reused);
 const auto toClip=actors.at(to.key).actions.states.at(size_t(remoteParent.state));remoteParent.frame=toClip.loopStart;
 from.playbacks[reused]=foreignParent;from.playbacks[reused].active=false;from.playbacks[reused].child=-1;
 to.playbacks[reused]=remoteParent;auto& remote=to.playbacks[reused];remote.mode=4;remote.desiredMode=2;remote.parent=int(toParent);remote.child=-1;
 foreign.playbackPool[reused]={{to.key.type,to.key.index},true,true};
 actors.restore(foreign);const auto foreignBytes=actors.snapshot();actors.restore(foreignBytes);assert(actors.snapshot()==foreignBytes);
 actors.update(1);const auto consumed=actors.cloneState();
 assert((consumed.playbackPool[reused].owner==edactions::PlaybackOwner{to.key.type,to.key.index}));
 assert(actors.at(to.key).actions.playbacks[reused].active&&actors.at(to.key).actions.playbacks[reused].mode==2);
 assert(!actors.at(from.key).actions.playbacks[reused].active&&actors.at(from.key).actions.playbacks[reused].mode==foreignParent.mode);
 const auto consumedBytes=actors.snapshot();actors.restore(consumedBytes);assert(actors.snapshot()==consumedBytes);
 vm.restore(originalVm);actors.restore(foreignBytes);actors.update(1);assert(actors.snapshot()==consumedBytes);
 vm.restore(originalVm);actors.restore(original);
 //4063fe/40640e uses the CURRENT target owner's clip when it has a child.
 auto chain=foreign;auto& target=*std::find_if(chain.bodies.begin(),chain.bodies.end(),[&](const auto& item){return item.key==to.key;});
 const auto& targetStates=actors.at(to.key).actions.states;unsigned targetState=0;
 while(targetState<targetStates.size()&&(!targetStates[targetState].animation||targetStates[targetState].finishStart==fromClip.finishStart))++targetState;
 assert(targetState<targetStates.size());const auto targetFinish=targetStates[targetState].finishStart;
 auto& currentTarget=target.playbacks[reused];currentTarget.state=int(targetState);currentTarget.frame=targetStates[targetState].start;currentTarget.child=48;
 target.playbacks[48]=target.playbacks[toParent];target.playbacks[48].mode=4;target.playbacks[48].parent=int(reused);target.playbacks[48].child=-1;
 chain.playbackPool[48]={{to.key.type,to.key.index},true,true};actors.restore(chain);
 actors.at(from.key).actions.tickSlot(fromSlot);
 const auto& changedTarget=actors.at(to.key).actions.playbacks[reused];
 assert(changedTarget.active&&changedTarget.mode==3&&changedTarget.frame==targetFinish);
 assert((actors.cloneState().playbackPool[reused].owner==edactions::PlaybackOwner{to.key.type,to.key.index}));
 assert(actors.at(from.key).actions.playbacks[reused].state==foreign.bodies.front().playbacks[reused].state&&
     actors.at(from.key).actions.playbacks[reused].mode==foreign.bodies.front().playbacks[reused].mode);
 const auto chainBytes=actors.snapshot();actors.restore(chainBytes);assert(actors.snapshot()==chainBytes);
 vm.restore(originalVm);actors.restore(original);
 // Scene4055c0 may clear the global target behind an Ash incoming pointer.
 // The dead record's mode byte has no observable consumer before allocation.
 auto cleared=state;auto& preserved=cleared.bodies.front();auto& outgoing=preserved.playbacks[fromSlot];
 outgoing.mode=0;outgoing.frame=(fromClip.clipFlags&8)?fromClip.earlyEnd:fromClip.end;outgoing.step=1;outgoing.child=int(reused);preserved.flags=1;
 assert(cleared.playbackPool[reused].owner.empty());actors.restore(cleared);actors.update(1);
 assert(actors.cloneState().playbackPool[reused].owner.empty());const auto clearBytes=actors.snapshot();actors.restore(clearBytes);assert(actors.snapshot()==clearBytes);
 vm.restore(originalVm);actors.restore(original);
 std::cout<<"Global cross-owner historical child/current-owner activation, cleared-target skip and exact replay: passed\n";
 // V10 lacksglobalownership evenifalllocalrecords happen tolookvalid.
 statebytes::Reader input{original};assert(input.text()=="EDACTORS13");const auto payload=input.cursor+501;
 statebytes::Writer old;old.text("EDACTORS10");old.bytes.insert(old.bytes.end(),original.begin()+payload,original.end()-127);
 rejected([&]{actors.restore(old.bytes);});assert(actors.snapshot()==original);
 auto truncated=original;truncated.resize(truncated.size()-1);rejected([&]{actors.restore(truncated);});assert(actors.snapshot()==original);
 // Physicalunregister doesnotrelease orstop the sourceglobalanimationrecord.
 actors.unregister({0,0});auto& ash=actors.at({0,0});const auto ashSlot=unsigned(ash.actions.layers[0].playback);
 const int before=ash.actions.playbacks[ashSlot].frame;actors.update(1);
 assert((actors.cloneState().playbackPool[ashSlot].owner==edactions::PlaybackOwner{0,0}));
 assert(ash.actions.playbacks[ashSlot].frame!=before);
 vm.restore(originalVm);actors.restore(original);
 // Nativeleafcompletion isprotected through actor/objectphysics, andreadyonly
 // immediatelybefore live39; visibility/freeze cannotcancel thatcompletion.
 auto pending=actors.cloneState();auto& victim=*std::find_if(pending.bodies.begin(),pending.bodies.end(),[](const auto& b){return b.key.type==4;});pendingSlot=victim.layers[0].playback;
 auto& clip=victim.playbacks[size_t(pendingSlot)];clip.active=false;clip.reusable=false;clip.child=-1;
 pending.playbackPool[size_t(pendingSlot)].active=false;pending.playbackPool[size_t(pendingSlot)].reusable=false;
 victim.flags=0x40;actors.restore(pending);const auto previousPhysics=physicsPhase;checking=true;actors.update(1);checking=false;assert(physicsPhase>previousPhysics);
 assert(actors.cloneState().playbackPool[size_t(pendingSlot)].reusable);
 // Globalpool/save/VMcontinue independentlyandreplayexactly.
 vm.restore(originalVm);actors.restore(original);actors.consoleAction({0,0},3);const auto waiting=actors.snapshot(),waitVm=vm.snapshot();
 for(unsigned n=1;n<=20;++n)actors.update(n);const auto after=actors.snapshot(),afterVm=vm.snapshot();
 vm.restore(waitVm);actors.restore(waiting);for(unsigned n=1;n<=20;++n)actors.update(n);
 assert(actors.snapshot()==after&&vm.snapshot()==afterVm);
 std::cout<<"ActualMain815 globalownership/unregister/physics-beforeLIVE39/cycle+oldsaveatomicreject/VMreplay: passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
