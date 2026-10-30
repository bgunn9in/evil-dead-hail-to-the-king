#include "gameplay_actors.h"
#include "source_actor_math.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
static jpc::Value I(int n){return jpc::Value::integer(n);}
static void algorithmTests(){
 using edactions::AnimationHeight;using edactions::PlaybackOwner;
 const PlaybackOwner ash{0,0},other{4,0};AnimationHeight h;std::vector<int> set;unsigned samples=0;
 auto root=[&]{++samples;return .8976688385009766f;};auto apply=[&](int cm){set.push_back(cm);};
 h.beforeFrame(ash,0x10,51,0,51,true,root,apply);assert(h.pending&&h.owner==ash&&set.empty()&&samples==1);
 h.beforeFrame(other,0,1,0,5,true,root,apply);assert(h.pending&&set.empty());
 h.beforeFrame(ash,0x20,0,0,33,true,root,apply);assert(!h.pending&&(set==std::vector<int>{0,89}));
 h.beforeFrame(ash,0x50,51,0,51,true,root,apply);assert(h.pending&&h.value==0&&samples==2);
 h.beforeFrame(ash,0,1,0,5,true,root,apply);assert(!h.pending&&set.back()==0);
 h.value=-.8976688385f;assert(h.centimeters(true)==-89&&h.centimeters(false)==-89);
 // SH4 rounds the multiply to float; PC x87 keeps the float operand exact through conversion.
 h.value=.01f;assert(h.centimeters(true)==1&&h.centimeters(false)==0);
 h.value=std::numeric_limits<float>::infinity();bool failed=false;try{h.centimeters(true);}catch(const std::runtime_error&){failed=true;}assert(failed);
 std::cout<<"HEIGHT_ALGORITHM_PASS deferred matching owner, reset/apply ordering, 0x40 precedence, source quantization\n";
}
static edactors::Host host(){edactors::Host h;
 h.spawnPose=[](auto,int,int,int){return edactors::Pose{};};h.floorHeight=[](float,float){return -10.35323143f;};
 h.floorHeightForActor=[](auto,float,float,int32_t&){return -10.35323143f;};
 h.lineOfSight=[](auto,auto){return true;};h.visibleArea=[](auto){return true;};h.facing=[](auto,auto,int){return true;};h.random=[](int){return 0;};
 h.move=[](auto& body,int f,int s,int v){edactormath::appendMovement(body.pendingMove,f,s,v,{{0,body.pose.yaw,0}},body.motionFlags);return 1;};
 h.applyMovement=[](auto& body){const auto m=edactormath::pendingToWorld(body.pendingMove,{{0,body.pose.yaw,0}},body.motionFlags);body.pose.x+=m[0];body.localY+=m[1];body.pose.y=body.cachedFloorHeight+body.localY;body.pose.z+=m[2];};
 h.pickupObjects=[](){return std::vector<edactors::PickupObject>{};};h.effect=[](const auto&,const auto&){return 0;};
 h.animationEvent=[](const auto&,const auto&){};h.animationSound=[](const auto&,const auto&){};h.soundMaterials=[](const auto&){return edanimsound::Materials{};};return h;
}
static void integrationTests(const std::filesystem::path& assets,bool shared){
 ScriptGame game(assets/"scripts/levelA.jpc");edactors::GameplayActors actors(game,assets,host(),shared);const auto ash=actors.initializeAsh();
 for(const auto& [name,mask]:std::vector<std::pair<const char*,int>>{{"flagSubEngine",8},{"flagAddEngine",1},{"flagSubEngine",32}})
  game.vm.invoke(game.package.methodIndex("ed/base/EDActor",name,"(I)V"),ash,{I(mask)});
 actors.sampleFloors();actors.captureLevelBoundary();uint64_t tick=0;
 auto step=[&]{actors.update(++tick);};
 assert(actors.consoleAction({0,0},128));bool saw149=false,sawPending=false,applied=false;
 std::vector<uint8_t> pendingActor,pendingVM,committedActor,committedVM;uint64_t pendingTick=0;
 for(unsigned n=0;n<100;++n){step();const auto s=actors.cloneState();const auto& b=actors.at({0,0});
  for(const auto& p:b.actions.playbacks)if(p.active&&p.state==149)saw149=true;
  if(s.animationHeight.pending){sawPending=true;assert(s.animationHeight.value==.8976688385009766f&&b.localY==0);
   pendingActor=actors.snapshot();pendingVM=game.vm.snapshot();pendingTick=tick;break;}}
 assert(saw149&&sawPending);assert((actors.cloneState().animationHeight.owner==edactions::PlaybackOwner{0,0}));
 // Frozen/invisible or paused playback cannot consume the pending register.
 actors.at({0,0}).flags|=0x40;step();assert(actors.cloneState().animationHeight.pending&&actors.at({0,0}).localY==0);
 game.vm.restore(pendingVM);actors.restore(pendingActor);tick=pendingTick;actors.update(++tick,false);assert(actors.cloneState().animationHeight.pending);
 game.vm.restore(pendingVM);actors.restore(pendingActor);tick=pendingTick;
 // Scene disposal and reset leave the register and its Ash playback owner alive.
 actors.unloadScene();actors.resetSceneRuntime();assert(actors.cloneState().animationHeight.pending);
 game.vm.restore(pendingVM);actors.restore(pendingActor);tick=pendingTick;
 step();const float expected=float(89)*.01f;
 assert(!actors.cloneState().animationHeight.pending&&actors.at({0,0}).localY==expected&&actors.at({0,0}).pose.y==-10.35323143f+expected);applied=true;
 committedActor=actors.snapshot();committedVM=game.vm.snapshot();const auto committedTick=tick;
 // Direct rollback, cold registry restore, and future execution agree byte for byte.
 game.vm.restore(pendingVM);actors.restore(pendingActor);tick=pendingTick;step();assert(actors.snapshot()==committedActor&&game.vm.snapshot()==committedVM);
 ScriptGame coldGame(assets/"scripts/levelA.jpc");edactors::GameplayActors cold(coldGame,assets,host(),shared);coldGame.vm.restore(pendingVM);cold.restore(pendingActor);
 assert(cold.snapshot()==pendingActor);cold.update(pendingTick+1);assert(cold.snapshot()==committedActor&&coldGame.vm.snapshot()==committedVM);
 for(unsigned n=0;n<12;++n)step();const auto futureActor=actors.snapshot(),futureVM=game.vm.snapshot();
 for(unsigned n=0;n<12;++n)cold.update(committedTick+n+1);assert(cold.snapshot()==futureActor&&coldGame.vm.snapshot()==futureVM);
 // An old save carries no historical deferred register; never infer roof elevation.
 statebytes::Reader reader{committedActor};assert(reader.text()=="EDACTORS13");statebytes::Writer old;old.text("EDACTORS11");old.bytes.insert(old.bytes.end(),committedActor.begin()+reader.cursor,committedActor.end()-127);
 game.vm.restore(committedVM);actors.restore(old.bytes);assert(!actors.cloneState().animationHeight.pending&&actors.cloneState().animationHeight.owner.empty());
 actors.restore(committedActor);tick=committedTick;const auto before=actors.snapshot();auto bad=actors.cloneState();bad.animationHeight.value=std::numeric_limits<float>::quiet_NaN();
 bool rejected=false;try{actors.restore(bad);}catch(const std::runtime_error&){rejected=true;}assert(rejected&&actors.snapshot()==before);
 // Source action129/state150 resets the absolute local offset on its start pass.
 assert(actors.consoleAction({0,0},129));bool saw150=false;for(unsigned n=0;n<100;++n){step();const auto& b=actors.at({0,0});
  for(const auto& p:b.actions.playbacks)if(p.active&&p.state==150&&p.mode!=4){saw150=true;}
  if(saw150&&b.localY==0)break;}
 assert(applied&&saw150&&actors.at({0,0}).localY==0);
 std::cout<<"HEIGHT_INTEGRATION_PASS "<<(game.profile.dreamcast?"DC":"PC")<<(shared?" global50":" local50")<<" original149/150, flags10/20, source89cm, pause/freeze, unload, direct/cold/future, V11 neutral, atomic invalid reject\n";
}
int main(int argc,char**argv){try{std::cout<<std::unitbuf;algorithmTests();if(argc!=2)throw std::runtime_error("Pass exported asset directory");for(bool shared:{true,false})integrationTests(argv[1],shared);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
