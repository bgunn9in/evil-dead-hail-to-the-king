#pragma once
// Native action/curve and explicit scene/Ash-position fixtures; no earned-walk claim.
namespace native_locomotion_test {
using Driver=native_combat_controls_test::Driver;
inline void require(bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("Native locomotion: ")+why);}
inline void tables(const fs::path& root){
    const auto states=edactions::load(root/"actors/ash/actions.bin");
    for(const auto family:std::array<std::array<int,4>,4>{{{{0,0,11,12}},{{85,28,39,40}},{{82,56,67,68}},{{83,84,95,96}}}}){
        edactions::ActorActions actor(states);if(family[0])require(actor.command(family[0]),"native equip family unavailable");
        for(unsigned n=0;n<60;++n)actor.tick();require(actor.layers[0].state==family[1],"native equipment idle differs");
        require(actor.command(3),"original forward3 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.layers[0].state==family[2]&&edlocomotion::manualAction(actor)==3,"walk classified as another action");
        require(actor.command(24),"original Run24 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.layers[0].state==family[3]&&edlocomotion::manualAction(actor)==145,"Run24 did not select real run clip");
        require(actor.command(25)&&actor.layers[0].state==family[2],"Run25 did not return to walk");
        require(actor.command(4),"run/walk source stop4 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.command(145),"native145 Run start rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(edlocomotion::manualAction(actor)==145&&!actor.possible(146),"Run145 or absent146 classified incorrectly");
        require(actor.command(4),"native Run release4 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.command(5),"original backward5 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(edlocomotion::backwardActive(actor)&&edlocomotion::manualAction(actor)==0,"backhop was treated as forward Run");
        require(!actor.possible(4)&&actor.command(6),"backward stop must be original6");for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.layers[0].state==family[1]&&!edlocomotion::backwardActive(actor),"backward release did not finish");
        require(actor.command(22),"native Raise22 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        const int raised=actor.layers[0].state;require(actor.command(145),"armed Run145 rejected");for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.layers[0].state==raised&&edlocomotion::manualAction(actor)==145,"armed Run lost original base/overlay");
        require(actor.command(25)&&edlocomotion::manualAction(actor)==3,"armed Run25 lost walk overlay");
        actor.command(4);for(unsigned n=0;n<60;++n)actor.tick();
        require(actor.layers[0].state==raised&&edlocomotion::manualAction(actor)==0,"armed locomotion stop replaced raised base");
    }
}
struct Sample {int state=-1,frame=0;uint32_t animation=0,flags=0;bool active=false;};
inline Sample sample(const edactions::ActorActions& a){int index=edlocomotion::rootLayer(a).playback;if(index<0)return {};
    if(a.playbacks.at(size_t(index)).mode==4&&a.playbacks.at(size_t(index)).parent>=0)index=a.playbacks.at(size_t(index)).parent;
    const auto& p=a.playbacks.at(size_t(index));const auto& s=a.states.at(size_t(p.state));
    if(!p.active||p.frame<0||unsigned(p.frame)>=s.frames)return {};
    return {p.state,int(edactions::curveFrame(s,p)),s.animation,s.clipFlags,p.active};
}
inline void gait(Driver& d,bool run){
    auto& w=d.world;const auto mesh=edmesh::ActorMesh::load(w.root/"actors/ash/actor-parts.bin");
    const uint32_t expected=w.actors->at({0,0}).actions.states.at(run?12:11).animation;
    unsigned matched=0,wraps=0;double requested=0,moved=0;
    d.raw.keys[SDL_SCANCODE_LSHIFT]=run;
    for(unsigned n=0;n<100;++n){
        d.raw.keys[SDL_SCANCODE_W]=(n/8)%2==0;d.raw.keys[SDL_SCANCODE_S]=(n/8)%2!=0;
        const auto before=sample(w.actors->at({0,0}).actions);const Vec2 position{w.actor.position[0],w.actor.position[2]};
        d.frame();const auto after=sample(w.actors->at({0,0}).actions);
        require(w.actor.clearanceRadius==0,"native pose publication reintroduced modern static-wall clearance metadata");
        require(w.scene.id=="ak01","bounded gait fixture left its source room");
        const auto& step=w.nativeMovementStep;requested+=step.requested;moved+=step.moved;
        require(std::abs(step.moved-length(Vec2{w.actor.position[0],w.actor.position[2]}-position))<.00002f,"World added a second manual translation after native physics");
        if(!before.active||!after.active||before.state!=after.state||before.animation!=expected||after.animation!=expected)continue;
        std::array<float,3> delta{};
        if(after.frame<=before.frame){++wraps;require((after.flags&1)!=0,"unexpected nonloop gait frame order");}
        else{const auto a=mesh.rootPosition(expected,unsigned(before.frame)),b=mesh.rootPosition(expected,unsigned(after.frame));
            delta={{a[0]-b[0],0,b[2]-a[2]}};}
        const auto& ash=w.actors->at({0,0});const auto world=edactormath::pendingToWorld(delta,{{0,ash.pose.yaw,0}},ash.motionFlags);
        require(std::abs(step.desiredDelta.x-world[0])<.00002f&&std::abs(step.desiredDelta.z-world[2])<.00002f,"World displacement is not the original per-frame gait curve");++matched;
    }
    d.raw.keys[SDL_SCANCODE_W]=d.raw.keys[SDL_SCANCODE_S]=d.raw.keys[SDL_SCANCODE_LSHIFT]=false;d.frames(25);
    require(matched>60&&wraps>=2&&requested>2&&moved>.1,"full native gait cycles/physical movement were not exercised");
    std::cout<<"Native gait "<<(run?"run145":"walk3")<<" curveFrames"<<matched<<" wraps"<<wraps<<" requested"<<requested<<" moved"<<moved<<'\n';
}
inline void backhopStop(const fs::path& root){Driver d(root);auto& w=d.world;
    d.raw.keys[SDL_SCANCODE_Z]=true;d.frame();d.raw.keys[SDL_SCANCODE_Z]=false;
    unsigned skipped=0;double moved=0;
    for(unsigned n=0;n<65;++n){const auto& a=w.actors->at({0,0}).actions;int index=edlocomotion::rootLayer(a).playback;
        if(a.playbacks.at(size_t(index)).mode==4&&a.playbacks.at(size_t(index)).parent>=0)index=a.playbacks.at(size_t(index)).parent;
        const auto before=a.playbacks.at(size_t(index));d.frame();const auto& after=w.actors->at({0,0}).actions.playbacks.at(size_t(index));
        moved+=w.nativeMovementStep.moved;
        if(before.active&&before.mode==6&&after.active&&after.mode==3&&before.state==after.state){
            const auto& state=a.states.at(size_t(after.state));if(before.frame+before.step>state.earlyEnd){++skipped;
                require(w.nativeMovementStep.requested<.00001f,"deferred backhop stop translated the skipped animation segment after landing");}}
    }
    require(skipped==1&&moved>.2&&!edlocomotion::backwardActive(w.actors->at({0,0}).actions),"quick backhop did not jump/land/finish its authored sequence");
    std::cout<<"Native backhop stop: source6 deferred tail resets both frames; no extra post-landing translation; moved"<<moved<<'\n';
}
inline void savedTurn(const fs::path& root){Driver d(root);auto& w=d.world;
    // A native-global serialization fixture; no player stock, HP or actor AI writes.
    w.nativeProperties.turnAmount=8;const auto eight=w.saveStateBytes();w.nativeProperties.turnAmount=12;const auto twelve=w.saveStateBytes();
    require(eight.size()==twelve.size(),"turn global changed saved table size");size_t field=eight.size();unsigned differences=0;
    for(size_t n=0;n<eight.size();++n)if(eight[n]!=twelve[n]){field=n;++differences;}
    require(differences==1&&eight[field]==8&&twelve[field]==12&&field+4<=eight.size(),"turn global is not an isolated serialized integer");
    w.restoreStateBytes(twelve);d.input.reset();require(w.nativeProperties.turnAmount==12&&w.saveStateBytes()==twelve,"turn12 full World restore differs");
    World cold(root,nullptr,presentMovieHeadless,true);cold.restoreStateBytes(twelve);require(cold.saveStateBytes()==twelve,"cold saved native turn differs");
    for(unsigned n=0;n<3;++n){d.frame();ControlFrame neutral;cold.playerControls(neutral);cold.step({},false,false,.033f);}
    require(w.nativeProperties.turnAmount==12&&cold.saveStateBytes()==w.saveStateBytes(),"neutral/cold future lost cached NPC-visible turn amount");
    d.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,true);d.frames(6);const auto before=w.saveStateBytes();const auto held=w.nativeTurnHeld;const auto directions=w.nativeDirections;
    auto corrupt=twelve;const int32_t invalid=13;std::memcpy(corrupt.data()+field,&invalid,4);bool rejected=false;
    try{w.restoreStateBytes(corrupt);}catch(const std::exception&){rejected=true;}
    require(rejected&&w.saveStateBytes()==before&&w.nativeTurnHeld==held&&w.nativeDirections==directions,"invalid turn restore changed live World/input ownership");
    auto legacy=eight;legacy.erase(legacy.begin()+field,legacy.begin()+field+4);statebytes::Reader header{legacy};require(header.text()=="EDPLAY1","save header differs");
    const uint32_t version=19;std::memcpy(legacy.data()+header.cursor,&version,4);w.restoreStateBytes(legacy);d.input.reset();d.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,false);
    require(w.nativeProperties.turnAmount==8&&w.saveStateBytes()==eight,"legacy19 did not default missing turn global to native initialized8");
    std::cout<<"Native turn save "<<root<<":20/cold/neutral3future/invalid13 atomic/19 backread passed\n";
}
inline void fifoControls(const fs::path& root){Driver d(root);auto& w=d.world;auto& vm=w.game.vm;
    ControlFrame quick;quick.edges[size_t(ControlAction::Forward)]=true;w.playerControls(quick);
    require(w.nativeInputQueue.records.size()==2&&w.nativeInputQueue.records[0].action==3&&w.nativeInputQueue.records[1].action==4,
        "quick Forward pulse lost its original press3/release4 pair");w.resetNativeController();
    ControlFrame opposed;for(auto action:{ControlAction::Forward,ControlAction::Backward})opposed.down[size_t(action)]=opposed.edges[size_t(action)]=true;
    w.playerControls(opposed);require(w.nativeInputQueue.records.size()==2&&w.nativeInputQueue.records[0].action==3&&w.nativeInputQueue.records[1].action==5,
        "simultaneous native Up/Down did not preserve original3 then5 producer order");w.resetNativeController();
    // Explicit controller-freeze fixture: original8c06b240 returns before device update and FIFO drain.
    auto& body=w.actors->at({0,0});const auto flags=body.flags;const int turn=w.nativeProperties.turnAmount;
    ControlFrame frozen;frozen.down[size_t(ControlAction::TurnLeft)]=true;w.sampleNativeController(frozen);w.enqueueNativeInput(16,1,false);body.flags|=0x40;
    w.step({},false,false,.033f);require(w.nativeInputQueue.records.size()==1&&w.nativeTurnHeld[0]==0&&w.nativeProperties.turnAmount==turn,
        "original actor40 controller fence consumed queued input or advanced physical turn counters");
    body.flags=flags;w.sampleNativeController({});w.step({},false,false,.033f);require(w.nativeInputQueue.records.empty(),"unfrozen controller did not resume retained FIFO input");
    d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);d.frames(30);require(d.armed(),"menu gate fixture did not ready weapons");
    d.button(SDL_GAMEPAD_BUTTON_SOUTH,true);d.button(SDL_GAMEPAD_BUTTON_START,true);d.frame();
    const int attackStamp=vm.field(w.game.ash,125).integer();
    require(w.game.mode==0&&vm.field(w.game.ash,1188).integer()!=0&&w.nativeInputQueue.records.size()==1&&w.nativeInputQueue.records[0].action==112,
        "A+Start did not execute attack before retaining queued inventory request");
    const int requestTime=vm.staticField(398).integer();d.frame();
    require(w.game.mode==0&&w.nativeInputQueue.records.empty()&&vm.field(w.game.ash,126).integer()==requestTime&&vm.field(w.game.ash,125).integer()==attackStamp,
        "queued112 bypassed original attack gate or repeated attack");
    d.button(SDL_GAMEPAD_BUTTON_SOUTH,false);d.button(SDL_GAMEPAD_BUTTON_START,false);const int releaseTime=vm.staticField(398).integer();d.frame();
    require(vm.field(w.game.ash,126).integer()==releaseTime&&vm.field(w.game.ash,125).integer()==attackStamp,"queued release16 did not stamp only126");
    d.frames(60);d.button(SDL_GAMEPAD_BUTTON_START,true);d.frame();
    require(w.game.mode==2&&w.takeNativeModeChange(),"allowed original112 did not open actual inventory");
    const int weapon=vm.field(w.game.ash,125).integer(),lastInput=vm.field(w.game.ash,126).integer();const bool armed=d.armed();
    require(w.enqueueNativeInput(15,1),"old actor record enqueue failed");ControlFrame neutral;w.sampleNativeController(neutral);w.step({},false,false,.033f);
    require(w.game.mode==2&&w.nativeInputQueue.records.empty()&&vm.field(w.game.ash,125).integer()==weapon&&vm.field(w.game.ash,126).integer()==lastInput&&d.armed()==armed,
        "old actor15 was remapped/dispatched to Ash after GUI callback switch");
    w.closeInventory();require(w.requestPlayerInterface(3),"pause request enqueue failed");w.step({},false,false,.033f);
    require(w.game.mode==3&&w.takeNativeModeChange(),"allowed original111 did not open actual pause interface");
    std::cout<<"Native FIFO World: A before deferred112, source attack eligibility, release126/125, positive112/111, oldActor15->rawGUI passed\n";
}
}
void testNativeLocomotionWorld(const fs::path& root){using namespace native_locomotion_test;
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize native locomotion proof");struct Quit{~Quit(){SDL_Quit();}}quit;std::cout<<std::unitbuf;
    tables(root);tables(root/"levelB");
    backhopStop(root);backhopStop(root/"levelB");
    {Driver d(root);gait(d,false);gait(d,true);
        d.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);d.frames(38);require(edlocomotion::backwardActive(d.world.actors->at({0,0}).actions),"mapped DpadDown did not enter original backhop/backwalk");
        const auto back=d.world.saveStateBytes();d.world.restoreStateBytes(back);d.input.reset();d.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN,false);d.frames(40);
        require(!edlocomotion::backwardActive(d.world.actors->at({0,0}).actions),"neutral after valid load kept human backwalk active");
        const float yaw=d.world.actors->at({0,0}).pose.yaw;d.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,true);d.frames(20);d.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,false);d.frames(2);
        require(std::abs(std::remainder(double(d.world.actors->at({0,0}).pose.yaw)-yaw,double(edactormath::Tau)))>.3,"mapped DpadLeft did not execute native turn callback");
        d.button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true);d.frames(20);d.button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,false);d.frames(2);
        require(std::abs(std::remainder(double(d.world.actors->at({0,0}).pose.yaw)-yaw,double(edactormath::Tau)))<.00002,"native opposite turn ramps did not cancel");
    }
    savedTurn(root);savedTurn(root/"levelB");fifoControls(root);
    std::cout<<"Native locomotion passed:4weaponfamilies A/B, original gait phases/velocity, Back5/6, Dpad turns, native turn save state\n";
}
