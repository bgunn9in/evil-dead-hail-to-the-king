#pragma once
// A fresh real World/input regression, not an earned-story or walking-route fixture.
void testArmedLocomotionWorld(World& world) {
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Original armed locomotion: ")+message);};
    require(world.actors&&world.scene.id=="ak01","requires the fresh original AK01 World");
    const auto initial=world.saveStateBytes();auto& vm=world.game.vm;const int health=vm.field(world.game.ash,13).integer();
    const std::array<int,6> missions{{430,435,438,440,457,686}};std::array<int,6> before{};
    for(size_t n=0;n<missions.size();++n)before[n]=vm.staticField(missions[n]).integer();
    auto drive=[&](Vec2 stick,bool run,bool aim=false){ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;input.down[size_t(ControlAction::Run)]=run;world.playerControls(input);
        const auto moved=world.step(stick,run,false,.033f);require(!world.scriptFault,"original source frame failed");
        require(vm.field(world.game.ash,13).integer()==health,"nonattacking control regression changed health");return moved;};
    auto neutral=[&](int idle){unsigned stable=0;for(unsigned n=0;n<120;++n){if(world.game.mode!=0)world.closeInventory();drive({},false);const auto& ash=world.actors->at({0,0});
            // Initial source pickups can open inventory on the next Main frame.
            const bool ready=ash.actions.layers[0].state==idle&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&world.game.mode==0&&vm.staticField(844).integer()==0;
            stable=ready?stable+1:0;if(stable==3)return;
        }throw std::runtime_error("Original armed locomotion: source idle did not become ready");};
    auto held=[&](Vec2 stick,bool run,int expected,bool aim=false,int raised=-1){unsigned stable=0;for(unsigned n=0;n<60&&stable<3;++n){const auto moved=drive(stick,run,aim);
            if(moved.requested<=.001f||moved.moved<=.001f){const auto& ash=world.actors->at({0,0});std::cout<<"Armed held diagnostic: expected"<<expected<<" frame"<<n<<" run"<<run<<" aim"<<aim
                <<" actual"<<ash.actions.layers[0].state<<" tick"<<world.sourceTick<<" requested"<<moved.requested<<" moved"<<moved.moved<<" busy"<<vm.field(world.game.ash,11).integer()
                <<" aimField"<<vm.field(world.game.ash,123).integer()<<" modes"<<world.game.mode<<'/'<<vm.staticField(844).integer()<<" flags"<<ash.flags<<" canWalk3="<<ash.actions.possible(3)
                <<" canRun145="<<ash.actions.possible(145)<<" pose"<<ash.pose.x<<','<<ash.pose.z<<'\n';}
            // Original outgoing/queued clips finish before the next locomotion curve takes over.
            if(moved.requested<=.001f||moved.moved<=.001f){stable=0;continue;}
            if(!aim)require(world.actors->at({0,0}).actions.layers[0].state==expected,"movement selected a different original state");
            else {
                const auto& actions=world.actors->at({0,0}).actions;
                int target=-1;for(const auto& rule:actions.states.at(size_t(raised)).transitions)if(rule.action==3&&rule.target<actions.states.size())target=rule.target;
                require(actions.layers[0].state==raised&&vm.field(world.game.ash,123).integer()==1,"Aim walking lost the authored raised base");
                require(target>=0&&std::any_of(actions.layers.begin()+1,actions.layers.end(),[&](const auto& layer){return layer.state==target&&layer.playback>=0&&actions.playbacks[size_t(layer.playback)].active;}),"Aim walking omitted the authored partial locomotion layer");
            }++stable;}require(stable==3,"held movement was filtered or blocked");};
    for(const auto family:std::array<std::array<int,4>,2>{{{{10,0,11,12}},{{40,56,67,68}}}}){
        world.openInventory();world.syncInventory();const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
            [&](const auto& entry){return entry.containerField==98&&entry.itemID==family[0];});
        require(item!=world.inventoryView.items.end(),"fresh source-owned Axe/Gun is absent");world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());world.applyInventoryItem();
        require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==family[0],"real inventory did not equip the owned weapon");neutral(family[1]);
        // Alternate directions to stay within the original source start point's clear floor.
        held({.2f,0},false,family[2]);held({.2f,0},true,family[3]);drive({},true);neutral(family[1]);
        held({-.2f,0},true,family[3]);held({-.2f,0},false,family[2]);drive({},false);neutral(family[1]);
        int raised=-1;for(const auto& rule:world.actors->at({0,0}).actions.states.at(size_t(family[1])).transitions)if(rule.action==22&&rule.target<world.actors->at({0,0}).actions.states.size())raised=rule.target;
        require(raised>=0,"source idle lacks its authored Raise22 target");
        drive({},false,true);require(vm.field(world.game.ash,123).integer()==1,"original Aim did not enter");
        held({.2f,0},false,family[2],true,raised);drive({},false,true);drive({},false);neutral(family[1]);
    }
    // Explicit source-event fixture: zero damage starts the genuine hit wait/busy
    // path. No host field assignment, injected HP value or quest change is used.
    const auto reaction=vm.start(world.game.package.methodIndex("Ash","eventAttackHit","(IIII)V"),world.game.ash,
        {jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(40),jpc::Value::integer(0)});
    require(vm.status(reaction)==jpc::Interpreter::ThreadStatus::Waiting&&vm.field(world.game.ash,11).integer()>0,"zero-damage source reaction did not park as busy");
    const bool permitsForward=!vm.staticField(394).integer()&&world.actors->at({0,0}).actions.possible(145);
    const auto blocked=drive({.2f,0},true);const int action=edlocomotion::manualAction(world.actors->at({0,0}).actions);
    std::cout<<"Parked source callback: permitsForward"<<permitsForward<<" actualAction"<<action<<" requestedRoot"<<blocked.requested<<" movedRoot"<<blocked.moved<<'\n';
    require(permitsForward?action==145:action!=145,"callback movement differs from the original FSM/394 gate");
    for(size_t n=0;n<missions.size();++n)require(vm.staticField(missions[n]).integer()==before[n],"nonattacking movement changed a future mission");
    world.restoreStateBytes(initial,nullptr);require(world.saveStateBytes()==initial,"full original World did not restore exactly after the control regression");
    std::cout<<"Original armed locomotion: real owned UI Axe10/Gun40, held walk/run3frames, release, walk-run transitions, Aim, source parked callback/FSM gate and exact World restore passed\n";
}
