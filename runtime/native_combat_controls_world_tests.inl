#pragma once
// Explicit source-scene/Ash-position fixtures isolate combat; no earned-walk claim.
namespace native_combat_controls_test {
inline void require(bool yes,const char* text){if(!yes)throw std::runtime_error(std::string("Native combat controls: ")+text);}
struct Driver {
    World world;
    ControlSettings settings;
    ControlInput input;
    ControlRawState raw;
    SDL_JoystickID id=0;
    SDL_Gamepad* pad=nullptr;
    SDL_Joystick* joystick=nullptr;
    explicit Driver(const fs::path& root):world(root,nullptr,presentMovieHeadless,true){
        id=attachVirtualGamepad();pad=checked(SDL_OpenGamepad(id),"Open native combat virtual pad");joystick=SDL_GetGamepadJoystick(pad);
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MIN),"Release native LT");
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,SDL_JOYSTICK_AXIS_MIN),"Release native RT");frames(90);
    }
    ~Driver(){if(pad)SDL_CloseGamepad(pad);if(id)SDL_DetachVirtualJoystick(id);}
    int hp()const{return world.game.vm.field(world.game.ash,13).integer();}
    bool armed()const{return world.game.vm.field(world.game.ash,123).integer()!=0;}
    jpc::Ref saw()const{return world.game.vm.field(world.game.ash,102).reference();}
    void button(SDL_GamepadButton b,bool down){checked(SDL_SetJoystickVirtualButton(joystick,b,down),"Set native combat pad button");}
    void trigger(bool down){checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,down?SDL_JOYSTICK_AXIS_MAX:SDL_JOYSTICK_AXIS_MIN),"Set native combat LT");}
    ControlFrame frame(){
        if(world.game.mode==2)world.closeInventory();
        SDL_UpdateGamepads();raw.gamepad=id;
        for(unsigned n=0;n<raw.buttons.size();++n)raw.buttons[n]=SDL_GetGamepadButton(pad,SDL_GamepadButton(n));
        raw.leftTrigger=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER);raw.rightTrigger=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        const auto f=input.update(settings,raw);world.playerControls(f);
        world.step(f.movement,f.held(ControlAction::Run),f.pressed(ControlAction::Use),.033f);
        require(!world.scriptFault,world.game.diagnostic.c_str());if(world.takeNativeAimCancellation()&&f.held(ControlAction::Aim))input.cancelAimUntilRelease();return f;
    }
    void frames(unsigned n){while(n--)frame();}
    void pulse(SDL_GamepadButton b,ControlAction action){button(b,true);const auto f=frame();require(f.pressed(action),"default pad mapping lost its edge");button(b,false);frame();}
    void lower(){button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);frames(30);button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);frames(40);require(!armed(),"explicit LB release did not lower weapons");}
    void rev(){trigger(true);auto f=frame();require(f.pressed(ControlAction::ChainsawRev)&&!f.held(ControlAction::Aim),"native LT is not an independent Rev edge");
        int weaponStamp=world.game.vm.field(world.game.ash,125).integer(),releaseTime=world.game.vm.staticField(398).integer();trigger(false);frame();
        require(world.game.vm.field(world.game.ash,126).integer()==releaseTime&&world.game.vm.field(world.game.ash,125).integer()==weaponStamp,"LT release171 lost idle timestamp or repeated Rev");frames(39);
        trigger(true);f=frame();require(f.pressed(ControlAction::ChainsawRev),"second LT press lost Rev");
        weaponStamp=world.game.vm.field(world.game.ash,125).integer();releaseTime=world.game.vm.staticField(398).integer();trigger(false);frame();
        require(world.game.vm.field(world.game.ash,126).integer()==releaseTime&&world.game.vm.field(world.game.ash,125).integer()==weaponStamp,"second LT release repeated Rev");frames(69);
        require(world.game.vm.field(saw(),139).integer()!=0&&world.game.vm.field(saw(),157).integer()!=0,"native LT did not start the real motor");}
    void scene(const char* name){world.endEpisodeScene();Scene next=loadScene(world.root/name);Actor actor=resetActor(next,0);std::vector<GateState> gates(next.doors.size());
        world.enterSourceScene(next,actor,gates);world.scene=std::move(next);world.actor=std::move(actor);world.gates=std::move(gates);frames(45);}
    void approach(edactors::Key key,float distance){const auto npc=world.actors->at(key).pose;
        const double back=edactormath::nativeHeading(npc.yaw)+edactormath::Pi;
        for(unsigned n=0;n<16;++n){const double a=back+double(n)*edactormath::Tau/16;const Vec2 p{npc.x+distance*float(std::cos(a)),npc.z+distance*float(std::sin(a))};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{p.x,0,p.z});ash.pose.yaw=edactormath::modelYawFromHeading(std::atan2(double(npc.z)-p.z,double(npc.x)-p.x));world.publishProgressBody(ash);
            frame();return; // A source frame refreshes spatial relationships after the explicit pose fixture.
        }throw std::runtime_error("Native combat approach fixture has no original floor");}
    void owned(int item,int container){world.openInventory();world.syncInventory();const auto found=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto&i){return i.itemID==item&&i.containerField==container;});
        require(found!=world.inventoryView.items.end(),"actual owned inventory item is missing");world.inventoryView.selected=unsigned(found-world.inventoryView.items.begin());world.applyInventoryItem();world.closeInventory();}
};
inline void readyControls(const fs::path& root){Driver d(root);auto& vm=d.world.game.vm;
    for(auto key:{SDL_SCANCODE_J,SDL_SCANCODE_K}){d.lower();d.raw.keys[key]=true;d.frame();require(d.armed(),"first raw J/K did not ready weapons");d.frames(40);
        require(d.armed()&&vm.field(d.world.game.ash,1188).integer()==0,"unheld modern Aim cancelled original ready state or held key auto-attacked");
        const int weaponStamp=vm.field(d.world.game.ash,125).integer(),releaseTime=vm.staticField(398).integer();d.raw.keys[key]=false;d.frame();
        require(vm.field(d.world.game.ash,126).integer()==releaseTime&&vm.field(d.world.game.ash,125).integer()==weaponStamp,"raw A/X release lost idle timestamp or repeated attack");
        d.frames(1);d.raw.keys[key]=true;d.frame();require(vm.field(d.world.game.ash,1188).integer()!=0,"second raw J/K press did not start an original attack");d.raw.keys[key]=false;d.frames(50);}
    d.lower();d.rev();d.lower();
    d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);d.frames(30);require(d.armed(),"modern LB did not raise weapons");
    d.pulse(SDL_GAMEPAD_BUTTON_EAST,ControlAction::Use);d.frames(40);require(!d.armed(),"B/Use while holding LB was immediately re-raised");
    d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);d.frames(2);d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);d.frames(30);require(d.armed(),"LB release/repress did not recover Aim");d.lower();
    d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);d.frames(30);require(d.armed()&&d.world.modernAimHeld,"restore fixture did not hold explicit Aim");
    const auto saved=d.world.saveStateBytes();d.world.restoreStateBytes(saved);d.input.reset();d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false);d.frame();
    require(d.armed()&&!d.world.modernAimHeld,"successful restore retained physical Aim ownership and lowered saved ready weapons");
    d.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);d.frame();const auto before=d.world.saveStateBytes();bool rejected=false;
    try{d.world.restoreStateBytes(std::vector<uint8_t>{0});}catch(const std::exception&){rejected=true;}
    require(rejected&&d.world.modernAimHeld&&d.world.saveStateBytes()==before,"failed restore changed physical Aim ownership or world bytes");d.lower();
    std::cout<<"Native controls: raw J/K ready+second-edge attack without Space; no held repeat; LT ready/rev; explicit LB release/B cancel; atomic restore Aim ownership passed\n";
}
inline std::map<int,int> objects(const World& w){std::map<int,int> out;for(const auto&[id,o]:w.nativeProperties.objects)if(o.flags&1)out[id]=o.setup[0];return out;}
inline void finish(Driver& d,edactors::Key key){auto& w=d.world;auto& vm=w.game.vm;const auto target=w.actors->at(key).script;
    require(vm.field(target,1184).integer()!=0&&vm.field(target,13).integer()>0,"natural live ComboIdle window is absent");
    const int score=vm.staticField(72).integer(),ammo=vm.field(vm.field(w.game.ash,103).reference(),815).integer();const auto before=objects(w);
    // Another live NPC can interrupt Ash; retry real press edges after the original get-hit guard clears.
    for(unsigned n=0;n<6&&!vm.field(w.game.ash,96).integer();++n){d.approach(key,1.2f);d.pulse(SDL_GAMEPAD_BUTTON_SOUTH,ControlAction::PrimaryAttack);if(!vm.field(w.game.ash,96).integer())d.frames(8);}
    std::cout<<"COMBO_ENTRY combo="<<vm.field(w.game.ash,96).integer()<<" target="<<vm.staticField(97).reference()<<" expected="<<target<<" score="<<vm.staticField(72).integer()<<" before="<<score<<'\n';
    // Native processVFXAction decays72 each source frame after the combo callback adds100.
    require(vm.field(w.game.ash,96).integer()!=0&&vm.staticField(97).reference()==target&&vm.staticField(72).integer()>score,"native A did not enter original94 combo");
    for(unsigned n=0;n<80&&!vm.field(target,1185).integer();++n)d.frame();
    require(vm.field(target,1185).integer()!=0,"original NPC combo callback did not catch the target");
    for(unsigned n=0;n<80&&!w.actors->at({0,0}).actions.possible(92);++n)d.frame();require(w.actors->at({0,0}).actions.possible(92),"original finisher92 transition is unavailable");
    bool dead=false;std::set<int> drops;std::set<int> speech;
    const auto observe=[&]{dead|=vm.field(target,w.game.hostField(HostFieldRole::GenericNpc30)).integer()!=0;
        for(const auto&[slot,item]:objects(w))if(!before.count(slot)||before.at(slot)!=item)drops.insert(item);
        const int quote=vm.staticField(73).integer();for(const auto& voice:w.audioBank.snapshot().voices)if(voice.clip==quote&&!w.audioBank.done(voice.token))speech.insert(voice.token);};
    d.pulse(SDL_GAMEPAD_BUTTON_WEST,ControlAction::SecondaryAttack);observe();
    require(!vm.field(w.game.ash,96).integer()&&vm.field(target,13).integer()<=0,"native X did not finish the captured target");
    const auto left=vm.field(w.game.ash,103).reference();if(vm.field(left,812).integer()!=10)require(vm.field(left,815).integer()==ammo-1,"gun finisher did not consume exactly one real round");
    for(unsigned n=0;n<180;++n){d.frame();observe();}
    require(dead,"original finisher did not complete death");require(!speech.empty(),"original combo/victory speech did not reach the native audio bank");
    std::cout<<"Native combo "<<w.scene.id<<" weapon"<<vm.field(left,812).integer()<<": natural94->92/capture/death, heroHP"<<d.hp()<<", active drop IDs";
    for(int item:drops)std::cout<<' '<<item;std::cout<<", speech tokens";for(int voice:speech)std::cout<<' '<<voice;std::cout<<'\n';
}
inline void natural(const fs::path& root,bool b){Driver d(root);auto& w=d.world;auto& vm=w.game.vm;d.scene(b?"bt01":"ak09");
    const auto found=std::find_if(w.actors->bodies().begin(),w.actors->bodies().end(),[&](const auto&e){return e.first.type==4&&(e.second.flags&1)&&vm.field(e.second.script,13).integer()>0;});
    require(found!=w.actors->bodies().end(),"source scene did not naturally spawn a live Deadite");const auto key=found->first;const auto target=found->second.script;
    const int setupHP=d.hp();d.rev();const int armedHP=d.hp();d.approach(key,1.65f);
    std::cout<<"NATURAL_SETUP "<<w.scene.id<<" HP"<<setupHP<<"->"<<armedHP<<" weapon"<<vm.field(vm.field(w.game.ash,103).reference(),812).integer()<<" targetHP"<<vm.field(target,13).integer()<<'\n';
    for(unsigned n=0;n<1200&&!vm.field(target,1184).integer()&&vm.field(target,13).integer()>0;++n){
        // Stop at the original wounded threshold so the native brain can enter ComboIdle before another hit kills it.
        if(n%40==0&&vm.field(target,13).integer()>vm.field(target,1208).integer()){
            d.approach(key,1.65f);if(vm.field(target,1184).integer()||vm.field(target,13).integer()<=0)break;
            d.button(SDL_GAMEPAD_BUTTON_SOUTH,vm.field(target,13).integer()>vm.field(target,1208).integer());
        }else d.button(SDL_GAMEPAD_BUTTON_SOUTH,false);
        d.frame();require(d.hp()>0,"ordinary source fight killed Ash before ComboIdle");}
    d.button(SDL_GAMEPAD_BUTTON_SOUTH,false);d.frame();require(vm.field(target,1184).integer()!=0&&vm.field(target,13).integer()>0,"normal powered saw hits did not earn a natural ComboIdle window");
    const auto checkpoint=w.saveStateBytes();finish(d,key);
    if(!b){w.restoreStateBytes(checkpoint);require(w.saveStateBytes()==checkpoint,"natural wounded branch did not restore exactly");
        d.owned(40,98);if(!vm.field(vm.field(w.game.ash,103).reference(),815).integer())d.owned(3,99);d.frames(45);finish(d,key);}
}
}
void testNativeCombatControlsWorld(const fs::path& root){using namespace native_combat_controls_test;
    SDL_SetMainReady();checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize native combat controls proof");struct Quit{~Quit(){SDL_Quit();}} quit;
    std::cout<<std::unitbuf;readyControls(root);natural(root,false);natural(root/"levelB",true);
    std::cout<<"Native combat controls passed: no source HP/vulnerability/AI edits; explicit source-scene/Ash-position fixtures, real inventory only\n";
}
