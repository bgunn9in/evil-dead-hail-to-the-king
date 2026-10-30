#pragma once
// Fresh original World/control regression; no earned-story or combat claim.
void testChainsawControlsWorld(World& world) {
    auto require=[](bool yes,const char* text){if(!yes)throw std::runtime_error(std::string("Original chainsaw controls: ")+text);};
    require(world.originalRuntime&&world.actors&&world.scene.id=="ak01","requires the fresh original AK01 World");
    const auto initial=world.saveStateBytes();auto& vm=world.game.vm;const int health=vm.field(world.game.ash,13).integer();
    auto input=[&](bool aim,bool revEdge=false,bool stopEdge=false,bool heldRev=false){ControlFrame frame;
        frame.down[size_t(ControlAction::Aim)]=aim;frame.down[size_t(ControlAction::ChainsawRev)]=heldRev||revEdge;
        frame.down[size_t(ControlAction::ChainsawStop)]=stopEdge;frame.edges[size_t(ControlAction::ChainsawRev)]=revEdge;frame.edges[size_t(ControlAction::ChainsawStop)]=stopEdge;
        world.playerControls(frame);require(!world.scriptFault&&world.notice!="could not perform action","original logical control failed");};
    auto drive=[&](bool aim,bool heldRev=false){input(aim,false,false,heldRev);world.step({},false,false,.033f);
        require(!world.scriptFault,"original source frame failed");require(vm.field(world.game.ash,13).integer()==health,"nonattacking motor test changed health");};
    auto consume=[&](){for(unsigned n=0;n<8&&!world.nativeInputQueue.records.empty();++n)world.step({},false,false,.033f);
        require(!world.scriptFault&&world.nativeInputQueue.records.empty(),"logical motor inputs did not drain through native FIFO");};
    unsigned stable=0;
    for(unsigned n=0;n<180&&stable<3;++n){if(world.game.mode!=0)world.closeInventory();drive(false);const auto& body=world.actors->at({0,0});
        const bool ready=world.game.mode==0&&vm.staticField(844).integer()==0&&!(body.flags&0x48)&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&body.actions.possible(116);
        stable=ready?stable+1:0;}
    require(stable==3,"startup UI/source input did not settle for three neutral frames");
    const auto saw=vm.field(world.game.ash,102).reference();
    require(saw&&vm.field(saw,812).integer()==100&&vm.field(saw,815).integer()==50,"fresh original primary Chainsaw100/fuel50 is absent");
    auto state=[&](){return vm.field(saw,139).integer();};auto motor=[&](){return vm.field(saw,157).integer()!=0;};
    auto canFire=[&](){return vm.field(saw,811).integer()!=0;};auto timer=[&](){return vm.field(saw,162).integer();};
    require(state()==0&&!motor(),"fresh original motor was already running");
    input(true);consume();for(unsigned n=0;n<30;++n)drive(true);
    input(true,true);consume();
    require(vm.field(world.game.ash,123).integer()==1&&(state()==1||state()==2)&&motor()&&canFire(),"queued110 did not start the motor before its normal source-frame idle update");
    for(unsigned n=0;n<180&&state()!=2;++n)drive(true);
    require(state()==2&&motor()&&canFire(),"original weaponProcessFrame did not settle start1 to idle2");
    for(unsigned n=0;n<40;++n)drive(true); // Release LT and finish the authored start animation before the next press.
    input(true,true);consume();
    require(state()==3&&motor()&&canFire()&&timer()==90,"second queued Rev edge did not enter original rev3/timer90");
    const int level=vm.field(saw,163).integer(),countdown=timer();const uint64_t tick=world.sourceTick;
    for(unsigned n=0;n<3;++n)drive(true,true);
    std::cout<<"Chainsaw held diagnostic: timer"<<countdown<<"->"<<timer()<<" ticks"<<tick<<"->"<<world.sourceTick<<" state"<<state()<<" motor"<<motor()<<" level"<<level<<"->"<<vm.field(saw,163).integer()<<'\n';
    require(world.sourceTick>tick&&timer()<countdown&&timer()>0&&state()==3&&motor()&&
        vm.field(saw,163).integer()==level,"holding Rev without edges refreshed the source countdown/rev level");
    const auto revved=world.saveStateBytes();for(unsigned n=0;n<3;++n)drive(true,true);const auto replay=world.saveStateBytes();
    world.restoreStateBytes(revved,nullptr);require(world.saveStateBytes()==revved,"revving full World did not restore byte-exactly");
    for(unsigned n=0;n<3;++n)drive(true,true);require(world.saveStateBytes()==replay,"rev timer/audio/VFX full World replay differs");
    input(true,false,true);consume();
    require(vm.field(world.game.ash,123).integer()==1&&state()==0&&!motor()&&!canFire()&&vm.field(saw,163).integer()==0,
        "Stop edge did not execute source108 while retaining Aim");
    input(true,true);consume();require((state()==1||state()==2)&&motor(),"real Rev edge did not restart the stopped source motor");
    for(unsigned n=0;n<180&&(state()!=2||!world.actors->at({0,0}).actions.possible(23));++n)drive(true);
    require(state()==2&&world.actors->at({0,0}).actions.possible(23),"source motor/Lower transition did not become ready");
    for(unsigned n=0;n<120&&(vm.field(world.game.ash,123).integer()||motor());++n)drive(false);
    require(!vm.field(world.game.ash,123).integer()&&state()==0&&!motor()&&!canFire(),"ordinary Aim release23 did not stop the motor");
    const int fuel=vm.field(saw,815).integer();require(fuel>=0&&fuel<=50,"source motor produced invalid fuel");
    world.restoreStateBytes(initial,nullptr);require(world.saveStateBytes()==initial,"complete initial World did not restore exactly");
    std::cout<<"Original chainsaw controls: source-owned100/fuel50, Aim+Rev110 start1/idle2, Rev3/timer90, held-edge suppression, revving World replay, Stop108 retaining Aim, Lower23 stop; remaining source fuel"<<fuel<<", exact initial restore passed\n";
}
