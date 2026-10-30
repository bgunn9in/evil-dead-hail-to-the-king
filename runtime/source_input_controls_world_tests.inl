#pragma once
// Fresh source/input fixtures; no earned-story or complete callback-lifecycle claim.
void testModernMovementRecoveryWorld(World& world) {
 auto require=[](bool yes,const char* message){if(!yes)throw std::runtime_error(message);};
 const auto clean=world.saveStateBytes();auto& vm=world.game.vm;const int health=vm.field(world.game.ash,13).integer();
 for(bool stale:{false,true}){
  world.restoreStateBytes(clean);
  require(world.actors->consoleAction({0,0},116),"Original pickup116 unavailable in recovery fixture");
  require(!world.actors->at({0,0}).actions.possible(3),"Pickup fixture did not block native walk3");
  // Also exercise the valid latch left by older saves/native3 during a pickup.
  if(stale)require(world.enqueueNativeInput(3,0x40),"Native3 recovery fixture queue refused input");
  unsigned walking=0;ControlFrame held;held.movement={.1f,0};
  for(unsigned n=0;n<120&&walking<3;++n){
   const bool blocked=!world.actors->at({0,0}).actions.possible(3)&&!edlocomotion::manualAction(world.actors->at({0,0}).actions);
   world.playerControls(held);const auto queued=world.nativeInputQueue.records.size();world.playerControls(held);
   require(world.nativeInputQueue.records.size()==queued,"Held recovery queued duplicate native3");
   const auto moved=world.step(held.movement,false,false,.033f);
   require(!world.scriptFault&&vm.field(world.game.ash,13).integer()==health,"Recovery source frame failed or changed health");
   if(!stale&&blocked)require(!vm.field(world.game.ash,124).integer(),"Modern movement latched3 while pickup rejected it");
   if(edlocomotion::manualAction(world.actors->at({0,0}).actions)==3&&moved.requested>.001f)++walking;
  }
  require(walking==3,"Held movement did not resume after original pickup animation");
  const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n){world.playerControls(held);world.step(held.movement,false,false,.033f);}
  const auto future=world.saveStateBytes();world.restoreStateBytes(saved);
  for(unsigned n=0;n<3;++n){world.playerControls(held);world.step(held.movement,false,false,.033f);}
  require(world.saveStateBytes()==future,"Pickup recovery save/future differs");
 }
 world.restoreStateBytes(clean);require(world.saveStateBytes()==clean,"Pickup recovery did not restore its exact fixture");
 std::cout<<"Modern movement: original pickup116 blocks3 without latching; held intent resumes stale/fresh124 once;3walking frames/save/future passed\n";
}
void testModernFacingWorld(World& world) {
 auto require=[](bool yes,const char* message){if(!yes)throw std::runtime_error(message);};const auto clean=world.saveStateBytes();
 auto frame=[&](const ControlFrame& f){world.playerControls(f);world.step(f.movement,false,false,.033f);require(!world.scriptFault,"Modern facing source frame failed");};
 ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;for(unsigned n=0;n<40;++n)frame(input);
 require(world.game.vm.field(world.game.ash,123).integer()!=0,"Modern facing fixture did not ready weapons");
 const auto& camera=world.scene.cameras[world.actor.camera];const float expected=std::atan2(camera.right.x,camera.right.z);
 const Vec2 start{world.actor.position[0],world.actor.position[2]};input.facing={1,0};frame(input);
 require(std::abs(std::remainder(double(world.actor.yaw)-expected,double(edactormath::Tau)))<.0001,"Right stick did not turn stationary aimed Ash to camera-relative direction");
 require(edlocomotion::manualAction(world.actors->at({0,0}).actions)==0&&length(Vec2{world.actor.position[0],world.actor.position[2]}-start)<.01f,"Right stick started locomotion");
 world.resetNativeController();require(length(world.modernFacing)==0&&!world.modernFacingMovement.held,"Focus/load reset retained modern facing intent");
 world.restoreStateBytes(clean);require(world.saveStateBytes()==clean,"Modern facing fixture did not restore exactly");
 std::cout<<"Modern facing: aimed stationary right stick turns without walking; reset clears intent; exact restore passed\n";
}
void testSourceTauntControl(World& world) {
 auto require=[](bool yes,const char* text){if(!yes)throw std::runtime_error(text);};auto& vm=world.game.vm;const auto initial=world.saveStateBytes();
 auto uses=[&](){int count=0;for(const auto& n:vm.arrayValues(vm.staticField(76).reference()))count+=n.integer();return count;};
 const int beforeUses=uses(),health=vm.field(world.game.ash,13).integer(),beforeVoice=world.audioBank.snapshot().nextVoice;
 ControlSettings settings;ControlInput controls;ControlRawState raw;controls.update(settings,raw);raw.keys[SDL_SCANCODE_T]=true;
 const auto pressed=controls.update(settings,raw);require(pressed.pressed(ControlAction::Taunt),"Default T did not produce a Taunt edge");world.playerControls(pressed);world.step({},false,false,.033f);
 require(!world.scriptFault&&world.notice!="could not perform action","Taunt control failed in the original source");
 const int clip=vm.staticField(73).integer();const auto sounds=world.audioBank.snapshot();
 const auto& quotes=vm.arrayValues(vm.staticField(75).reference());
 require(clip>=0&&std::any_of(quotes.begin(),quotes.end(),[&](const auto& q){return q.integer()==clip;})&&uses()==beforeUses+1,"Taunt19 did not select/count an original scene quote");
 require(std::any_of(sounds.voices.begin(),sounds.voices.end(),[&](const auto& v){return v.token>=beforeVoice&&v.clip==clip&&v.volume==100;}),"Original Taunt19 did not queue its real quote sound");
 world.playerControls(controls.update(settings,raw));world.step({},false,false,.033f);require(world.audioBank.snapshot().nextVoice==sounds.nextVoice&&uses()==beforeUses+1,"Held Taunt repeated without an edge");
 raw.keys[SDL_SCANCODE_T]=false;world.playerControls(controls.update(settings,raw));world.step({},false,false,.033f);raw.keys[SDL_SCANCODE_T]=true;world.playerControls(controls.update(settings,raw));world.step({},false,false,.033f);
 require(world.audioBank.snapshot().nextVoice==sounds.nextVoice&&uses()==beforeUses+1&&vm.field(world.game.ash,13).integer()==health,"Original active-quote guard failed or Taunt changed health");
 world.restoreStateBytes(initial);require(world.saveStateBytes()==initial,"Taunt input fixture did not restore the exact World");
 std::cout<<"Original Taunt control: T edge ->19 ->scene quote/audio100; held suppression/active-quote guard/exact restore passed\n";
}
void testSourceInputControlsWorld(World& world,const fs::path& captures={}) {
 auto require=[](bool okay,const char* message){if(!okay)throw std::runtime_error(message);};auto& vm=world.game.vm;const auto initial=world.saveStateBytes();
 const int health=vm.field(world.game.ash,13).integer();
 auto input=[](Vec2 move=Vec2{},bool aim=false){ControlFrame f;f.movement=move;f.down[size_t(ControlAction::Aim)]=aim;return f;};
 auto control=[&](const ControlFrame& f){world.playerControls(f);require(!world.scriptFault&&world.notice!="could not perform action","Actual consumer input failed");};
 auto frame=[&](ControlFrame f,bool run=false){f.down[size_t(ControlAction::Run)]=run;control(f);const auto step=world.step(f.movement,run,false,.033f);require(!world.scriptFault,"Actual source frame failed");require(vm.field(world.game.ash,13).integer()==health,"Nonattacking consumer regression changed HP");return step;};
 auto settle=[&](const std::string& stage){unsigned stable=0;for(unsigned n=0;n<180&&stable<3;++n){if(world.game.mode!=0)world.closeInventory();frame(input());const auto& body=world.actors->at({0,0});
  stable=world.game.mode==0&&vm.staticField(844).integer()==0&&!(body.flags&0x48)&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&body.actions.possible(116)?stable+1:0;}if(stable!=3){const auto& body=world.actors->at({0,0});std::cout<<"SETTLE FAIL stage="<<stage<<" scene="<<world.scene.id<<" tick="<<world.sourceTick<<" mode="<<world.game.mode<<" applied844="<<vm.staticField(844).integer()<<" guard394="<<vm.staticField(394).integer()<<" paused48="<<vm.staticField(48).integer()<<" overlay="<<world.overlayClockPaused<<" ash11="<<vm.field(world.game.ash,11).integer()<<" aim123="<<vm.field(world.game.ash,123).integer()<<" forward124="<<vm.field(world.game.ash,124).integer()<<" flags="<<body.flags<<" possible116="<<body.actions.possible(116)<<" possible23="<<body.actions.possible(23)<<" think="<<body.think<<'\n';
   for(unsigned i=0;i<7;++i){const auto& layer=body.actions.layers[i];std::cout<<"  L"<<i<<" state="<<layer.state<<" playback="<<layer.playback;if(layer.playback>=0){const auto& play=body.actions.playbacks[size_t(layer.playback)];std::cout<<" mode="<<play.mode<<" frame="<<play.frame<<" active="<<play.active<<" parent="<<play.parent<<" child="<<play.child;}std::cout<<'\n';}
   throw std::runtime_error("Actual input did not settle at "+stage);}};
 settle("startup");
 testModernMovementRecoveryWorld(world);
 testModernFacingWorld(world);
 testSourceTauntControl(world);
 {const int stamp=vm.staticField(398).integer();world.step({.1f,0},false,false,.033f);require(!world.scriptFault&&vm.field(world.game.ash,124).integer()==1&&vm.field(world.game.ash,126).integer()==stamp,"Direct World.step did not synchronize forward input");
  for(unsigned n=0;n<3;++n){world.step({.1f,0},false,false,.033f);require(!world.scriptFault&&vm.field(world.game.ash,126).integer()==stamp,"Direct held step produced metadata heartbeat");}
  world.step({},false,false,.033f);require(!vm.field(world.game.ash,124).integer()&&!vm.field(world.game.ash,123).integer(),"Direct stop did not release movement or alteredAim");settle("after-direct-step-stop");}
 {const auto clean=world.saveStateBytes();world.step({.1f,0},false,false,.033f);world.step({.1f,0},true,false,.033f);
  const auto& actions=world.actors->at({0,0}).actions;const auto& layer=actions.layers[0];
  std::cout<<"Unraised pending logical"<<layer.state<<" mode"<<actions.playbacks.at(size_t(layer.playback)).mode<<'\n';
  require(actions.states.at(size_t(layer.state)).animation==actions.states.at(12).animation&&actions.playbacks.at(size_t(layer.playback)).mode==4,"Gait switch did not exercise a logical pending Run");
  world.step({},true,false,.033f);for(unsigned n=0;n<5;++n)world.step({},false,false,.033f);
  require(world.actors->at({0,0}).actions.layers[0].state==0,"Pending Run neutral sent the preceding physical Walk stop");
  world.restoreStateBytes(clean);require(world.saveStateBytes()==clean,"Pending gait fixture did not restore exactly");}
 const auto held=input({.1f,0});require(!vm.field(world.game.ash,124).integer(),"Fresh forward latch is not released");
 auto actorBefore=world.actors->snapshot();const int firstTime=vm.staticField(398).integer();control(held);
 require(!vm.field(world.game.ash,124).integer()&&world.actors->snapshot()==actorBefore,"Enqueued3 ran before its native input phase");frame(held);
 require(vm.field(world.game.ash,124).integer()==1&&vm.field(world.game.ash,126).integer()==firstTime,"Queued original3 did not synchronize forward input/time");
 const int pauseStamp=vm.field(world.game.ash,126).integer();const auto pauseTick=world.sourceTick;
 world.setOverlayPaused(true);world.step({},false,false,0);
 require(vm.field(world.game.ash,124).integer()==1&&vm.field(world.game.ash,126).integer()==pauseStamp&&world.sourceTick==pauseTick,"Zero-dt focus/overlay frame invoked movement producer");
 world.setOverlayPaused(false);control(input());frame(input());settle("after-focus-pause");control(held);frame(held);
 const int heldTime=vm.field(world.game.ash,126).integer();for(unsigned n=0;n<3;++n){const auto before=vm.snapshot();actorBefore=world.actors->snapshot();control(held);
  require(vm.snapshot()==before&&world.actors->snapshot()==actorBefore,"Steady held input caused source heartbeat/native dispatch");frame(held);require(vm.field(world.game.ash,126).integer()==heldTime,"Steady held input refreshed126");}
 require(world.actors->at({0,0}).actions.layers[0].state==11,"Source forward walk FSM differs");frame(held,true);require(world.actors->at({0,0}).actions.layers[0].state==12,"Original run FSM differs");
 // Original unconditionally available AK01 pending-door helper establishes the
 // real45-tick394 guard; this is an explicit environment fixture, not an earned door claim.
 vm.invoke(world.game.package.methodIndex("GenericScene","portalEnableSoundPlay","(II)V"),0,{jpc::Value::integer(3),jpc::Value::integer(0x0e8a4aca)});
 require(world.scriptEffects()&&vm.staticField(394).integer()==1,"Original pending-door helper did not establish its guard");
 auto runningHeld=held;runningHeld.down[size_t(ControlAction::Run)]=true;auto runningNeutral=input();runningNeutral.down[size_t(ControlAction::Run)]=true;
 actorBefore=world.actors->snapshot();const int releaseTime=vm.staticField(398).integer();control(runningNeutral);
 require(vm.field(world.game.ash,124).integer()==1&&world.actors->snapshot()==actorBefore,"Enqueued4 ran before its native input phase");frame(input(),true);
 require(vm.field(world.game.ash,124).integer()==0&&vm.field(world.game.ash,126).integer()==releaseTime,"Queued release4 did not clear metadata during original394 guard");
 const int guardStamp=vm.field(world.game.ash,126).integer();unsigned guardedFrames=0;
 while(vm.staticField(394).integer()&&guardedFrames<60){const auto before=vm.snapshot();actorBefore=world.actors->snapshot();control(runningHeld);
  require(vm.snapshot()==before&&world.actors->snapshot()==actorBefore,"Deferred guarded3 touched VM or nativeFSM");frame(held,true);++guardedFrames;
  require(vm.field(world.game.ash,124).integer()==0&&vm.field(world.game.ash,126).integer()==guardStamp,"Guarded held move produced a126 heartbeat");}
 require(!vm.staticField(394).integer()&&guardedFrames>2,"Original45-tick guard did not expire naturally");
 actorBefore=world.actors->snapshot();const int clearTime=vm.staticField(398).integer();control(runningHeld);
 require(!vm.field(world.game.ash,124).integer()&&world.actors->snapshot()==actorBefore,"Guard-clear input dispatched before its source frame");
 const auto same=vm.snapshot();const auto queued=world.nativeInputQueue.records.size();control(runningHeld);require(vm.snapshot()==same&&world.nativeInputQueue.records.size()==queued,"Matched pending intent repeated producer3");frame(held,true);
 require(vm.field(world.game.ash,124).integer()==1&&vm.field(world.game.ash,126).integer()==clearTime,"Guard-clear held move did not reconcile exactly once");
 const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)frame(held);const auto replay=world.saveStateBytes();world.restoreStateBytes(saved);require(world.saveStateBytes()==saved,"Consumer moving save did not restore exactly");
 for(unsigned n=0;n<3;++n)frame(held);require(world.saveStateBytes()==replay,"Consumer held-input World replay differs");frame(input());settle("after-cold-replay-release");
 // Applied inventory mode owns input: no metadata, aim or attacks may run.
 world.openInventory();frame(input());require(vm.staticField(844).integer()==2,"Actual inventory did not applymode2");const auto paused=vm.snapshot();actorBefore=world.actors->snapshot();auto blocked=input({.1f,0},true);blocked.edges[size_t(ControlAction::PrimaryAttack)]=true;blocked.edges[size_t(ControlAction::Taunt)]=true;
 world.playerControls(blocked);require(vm.snapshot()==paused&&world.actors->snapshot()==actorBefore,"Applied inventory guard allowed gameplay input");world.closeInventory();settle("after-inventory-mode2");
 for(int family:{10,40}){
  world.openInventory();world.syncInventory();const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& item){return item.containerField==98&&item.itemID==family;});
  require(selected!=world.inventoryView.items.end(),"Actual source-owned weapon absent");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();settle(std::string("after-equip-")+std::to_string(family));
  const auto secondary=vm.field(world.game.ash,103).reference(),primary=vm.field(world.game.ash,102).reference();require(vm.field(secondary,812).integer()==family,"Actual UI equip did not select source weapon");
  const int ammo=vm.field(secondary,815).integer(),fuel=vm.field(primary,815).integer(),now=vm.staticField(398).integer();std::map<edactors::Key,int> hp;
  std::vector<int> expectedDraw;const auto& beforeActions=world.actors->at({0,0}).actions;
  for(const auto& layer:beforeActions.layers)if(layer.state>=0)for(const auto& rule:beforeActions.states.at(size_t(layer.state)).transitions)
   if(rule.action==22&&rule.target<beforeActions.states.size())expectedDraw.push_back(rule.target);
  require(!expectedDraw.empty(),"Original active FSM has no concrete22 draw target");
  for(const auto& [key,body]:world.actors->bodies())hp[key]=vm.field(body.script,13).integer();
  const auto beforeRaise=world.saveStateBytes();const auto beforeNative=world.actors->cloneState();
  auto damageEvents=[](const edactors::State& state){return std::count_if(state.messages.begin(),state.messages.end(),[](const auto& m){return m.event==5||(m.event>=36&&m.event<=42);});};
  const int chosen=world.actors->playerAction(17);const auto afterNative=world.actors->cloneState();
  std::cout<<"Direct actual original17 family"<<family<<" chosen"<<chosen<<" layers";for(const auto& layer:world.actors->at({0,0}).actions.layers)std::cout<<' '<<layer.state;
  std::cout<<" ammo"<<vm.field(secondary,815).integer()<<" fuel"<<vm.field(primary,815).integer()<<" HP"<<vm.field(world.game.ash,13).integer()<<" damageMessages"<<damageEvents(beforeNative)<<"->"<<damageEvents(afterNative)<<'\n';
  require(chosen==22&&vm.field(world.game.ash,123).integer()==1&&vm.field(world.game.ash,125).integer()==now,"Direct original17 was not sourceRaise22");
  require(vm.field(secondary,815).integer()==ammo&&vm.field(primary,815).integer()==fuel&&damageEvents(beforeNative)==damageEvents(afterNative),"Direct first17 fired or consumed ammo/fuel");
  for(const auto& [key,body]:world.actors->bodies())require(vm.field(body.script,13).integer()==hp.at(key),"Direct first17 applied damage");
  world.restoreStateBytes(beforeRaise);require(world.saveStateBytes()==beforeRaise,"Original raise diagnostic did not restore pre-input exactly");frame(input({},true));
  require(vm.field(world.game.ash,123).integer()==1&&vm.field(world.game.ash,125).integer()==now,"Stowed source17 did not Raise22 plus refresh125");
  require(vm.field(secondary,815).integer()==ammo&&vm.field(primary,815).integer()==fuel,"Aim consumed ammo/fuel");
  require(std::any_of(world.actors->at({0,0}).actions.layers.begin(),world.actors->at({0,0}).actions.layers.end(),[&](const auto& layer){return std::find(expectedDraw.begin(),expectedDraw.end(),layer.state)!=expectedDraw.end();}),"Aim did not select an authored native22 draw target");
  for(const auto& [key,body]:world.actors->bodies())require(vm.field(body.script,13).integer()==hp.at(key),"Aim applied actor damage");
  std::cout<<"Actual source-owned weapon"<<family<<" Aim17->22 targets";for(int target:expectedDraw)std::cout<<' '<<target;std::cout<<" timestamp125="<<now<<" ammo="<<ammo<<" fuel="<<fuel<<'\n';
  frame(input({},true));for(unsigned n=0;n<3;++n)frame(input({},true));require(vm.field(world.game.ash,125).integer()==now,"Held Aim refreshed weapon timestamp without edges");
  require(vm.field(secondary,815).integer()==ammo&&vm.field(primary,815).integer()==fuel,"Held Aim consumed ammo/fuel");
  const auto raised=world.saveStateBytes();
  auto authoredTarget=[&](int action){int target=-1;const auto& actions=world.actors->at({0,0}).actions;
   for(const auto& layer:actions.layers)if(layer.state>=0)for(const auto& rule:actions.states.at(size_t(layer.state)).transitions)
    if(rule.action==action&&rule.target<actions.states.size())target=rule.target;
   require(target>=0,"Raised original FSM has no concrete movement target");return target;};
  const int walkTarget=authoredTarget(3),runTarget=authoredTarget(145);
  auto partial=[&](int target){const auto& actions=world.actors->at({0,0}).actions;
   // Original Run24/25 can choose another authored state for the same root-owning clip (159 versus direct145 target160).
   return std::find_if(actions.layers.begin()+1,actions.layers.end(),[&](const auto& layer){if(layer.state<0||layer.playback<0)return false;
    const auto& state=actions.states.at(size_t(layer.state));return state.animation==actions.states.at(size_t(target)).animation&&!state.boneMask.empty()&&state.boneMask[0]&&actions.playbacks.at(size_t(layer.playback)).active;})!=actions.layers.end();};
  auto heldAim=[&](){require(vm.field(world.game.ash,123).integer()==1&&vm.field(world.game.ash,125).integer()==now&&vm.field(secondary,815).integer()==ammo&&vm.field(primary,815).integer()==fuel,
    "Movement-only step raised/lowered/fired instead of preserving current Aim");};
  auto neutralPartial=[&](bool run){world.step({},run,false,.033f);for(unsigned n=0;n<4;++n)world.step({},run,false,.033f);
   heldAim();require(!partial(walkTarget)&&!partial(runTarget)&&!vm.field(world.game.ash,124).integer()&&!world.actor.walking,
    "Held-Aim neutral retained a root-owning walk/run partial");};
  for(bool run:{false,true}){world.restoreStateBytes(raised);const int target=run?runTarget:walkTarget;
   unsigned movingFrames=0;for(unsigned n=0;n<24&&movingFrames<3;++n){const auto move=world.step({.1f,0},run,false,.033f);require(!world.scriptFault,"Held-Aim movement source frame failed");
    if(move.requested>0&&partial(target))++movingFrames;heldAim();}
   require(movingFrames==3,"Held-Aim movement lost its authored partial after the native queued/start phase");
   const auto moving=world.saveStateBytes();for(unsigned n=0;n<3;++n)world.step({.1f,0},run,false,.033f);const auto replay=world.saveStateBytes();
   world.restoreStateBytes(moving);require(world.saveStateBytes()==moving,"Raised movement did not restore exactly");for(unsigned n=0;n<3;++n)world.step({.1f,0},run,false,.033f);
   require(world.saveStateBytes()==replay,"Raised movement replay differs");neutralPartial(run);
   if(run&&family==40&&!captures.empty()){fs::create_directories(captures);auto write=[&](const fs::path& name,const auto& bytes){std::ofstream out(captures/name,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("Cannot retain input regression capture");};
    write("raised-running.world",moving);write("raised-neutral.world",world.saveStateBytes());}
  }
  // Stop logical Run145 with original4, including a queued preceding Walk tail.
  for(bool runFirst:{false,true}){world.restoreStateBytes(raised);world.step({.1f,0},runFirst,false,.033f);world.step({.1f,0},!runFirst,false,.033f);
   require(partial(runFirst?walkTarget:runTarget),"Held-Aim gait switch did not use authored target");
   const auto& actions=world.actors->at({0,0}).actions;bool waiting=false;
   for(const auto& layer:actions.layers)if(layer.state==(runFirst?walkTarget:runTarget)&&layer.playback>=0)waiting=actions.playbacks.at(size_t(layer.playback)).mode==4;
   std::cout<<"Raised partial family"<<family<<" switch"<<(runFirst?"run-walk":"walk-run")<<" pending"<<waiting<<'\n';
   // Raised3/145 create separate partial layers immediately; pending base
   // transitions are exercised below through the actual unraised gait switch.
   neutralPartial(!runFirst);
  }
  // A valid restore clears physical Aim ownership; explicitly press/release the modern button before holstering.
  frame(input({},true));frame(input());settle(std::string("after-Aim-release-")+std::to_string(family));
 }
 // Isolated original zero-damage callback fixture: check parked busy/input
 // behavior, then restore its valid pre-callback state (no completion claim).
 frame(held);const auto beforeHit=world.saveStateBytes();const auto reaction=vm.start(world.game.package.methodIndex("Ash","eventAttackHit","(IIII)V"),world.game.ash,
  {jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(40),jpc::Value::integer(0)});
 require(vm.status(reaction)==jpc::Interpreter::ThreadStatus::Waiting&&vm.field(world.game.ash,11).integer()>0&&!vm.field(world.game.ash,124).integer(),"Source zero-damage reaction did not reset input/busy state");
 actorBefore=world.actors->snapshot();const auto hitVm=vm.snapshot();control(held);
 require(world.actors->snapshot()==actorBefore&&vm.snapshot()==hitVm,"Busy input dispatched before its source frame");
 const bool hitPermitsRun=!vm.staticField(394).integer()&&world.actors->at({0,0}).actions.possible(145);
 const auto stopped=world.step(held.movement,true,false,.033f);const int hitAction=edlocomotion::manualAction(world.actors->at({0,0}).actions);
 require(!world.scriptFault&&(hitPermitsRun?hitAction==145:hitAction!=145)&&vm.field(world.game.ash,13).integer()==health,"Parked hit callback violated original movement FSM or changed HP");
 world.restoreStateBytes(beforeHit);require(world.saveStateBytes()==beforeHit,"Isolated reaction fixture did not restore pre-callback state exactly");
 const auto beforeReset=world.saveStateBytes(); vm.invoke(world.game.package.methodIndex("Ash","eventResetAnimations","()V"),world.game.ash);require(!vm.field(world.game.ash,124).integer(),"Original reset callback did not clear124");actorBefore=world.actors->snapshot();control(held);
 require(!vm.field(world.game.ash,124).integer()&&world.actors->snapshot()==actorBefore,"Reset input dispatched before its source frame");
 require(vm.field(world.game.ash,11).integer()==1,"Isolated original reset did not retain its source busy context");
 const bool resetPermitsWalk=!vm.staticField(394).integer()&&world.actors->at({0,0}).actions.possible(3);
 const int previousResetAction=edlocomotion::manualAction(world.actors->at({0,0}).actions);
 // Inspect the real input phase before this isolated callback's next scheduler/animation phase can change its FSM again.
 world.processNativeInputFrame();const int resetAction=edlocomotion::manualAction(world.actors->at({0,0}).actions);
 require(!world.scriptFault&&(resetPermitsWalk?resetAction==3:resetAction==previousResetAction),"Original reset callback input differs from its current FSM gate");
 world.step(held.movement,false,false,.033f);require(!world.scriptFault&&vm.field(world.game.ash,13).integer()==health,"Original reset callback source frame failed or changed health");
 world.restoreStateBytes(beforeReset);require(world.saveStateBytes()==beforeReset,"Scoped original reset fixture did not restore exactpre-callback World");
 world.restoreStateBytes(initial);require(world.saveStateBytes()==initial,"Full initial source World did not restore exactly");
 std::cout<<"Original input controls: source3/4 metadata edge/no native duplicate; guarddefer/release/clear once/no126heartbeat; actualAxe10/Gun40 stowedRaise22+125 noammo/fuel/HP drain; appliedUI/busy/reset guards; exact replay; guardedframes"<<guardedFrames<<'\n';
}
