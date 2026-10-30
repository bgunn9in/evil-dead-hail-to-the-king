#pragma once
// World-frame proof; declared source GUI calls configure only the timer fixture.
void testInventoryFrames(const fs::path& levelA) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize inventory frame proof");
    auto require=[](bool yes,const std::string& message){if(!yes)throw std::runtime_error("Original inventory frames: "+message);};
    auto I=[](int value){return jpc::Value::integer(value);};
    for(bool b:{false,true}) {
        World world(b?levelA/"levelB":levelA,nullptr,presentMovieHeadless,true);auto& vm=world.game.vm;const auto& p=world.game.package;
        auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);};
        world.closeInventory();for(unsigned n=0;n<2;++n)tick();world.openInventory();
        require(world.inventoryView.open&&world.game.mode==2&&vm.staticField(844).integer()==2&&vm.staticField(48).integer()==1,"actual Interface1147 did not apply inventory pause");
        const auto text=vm.invoke(p.methodIndex("ObjDB","getObjectTextInventoryDescription","(I)I"),0,{I(119)});
        require(!world.sourceTexts().lookup(episode_portal_detail::sceneId(world.scene.id),uint32_t(text.integer())).empty(),"original vitamin description is unavailable");
        vm.invoke(p.methodIndex("Game","showTextBox","(II)V"),0,{text,I(12)});
        const auto box=vm.staticField(851).reference();require(vm.field(box,390).integer()==12,"source GameTextBox807 timer was not initialized");
        const auto gui=vm.staticField(872).reference();
        vm.invoke(p.methodIndex("GuiSys","vfxShakeSystem","(III)V"),gui,{I(0),I(0),I(12)});
        require(vm.field(gui,837).integer()==12,"source GuiSys timed-effect fixture did not start");
        auto motion=[&](){statebytes::Writer out;for(const auto& body:world.actors->cloneState().bodies){
            out.put(body.key.type);out.put(body.key.index);for(float n:{body.pose.x,body.localY,body.pose.z,body.pose.yaw})out.put(n);
            for(float n:body.pendingMove)out.put(n);out.put(body.updates);
            for(const auto& layer:body.layers){out.put(layer.state);out.put(layer.playback);out.put(layer.historyIndex);for(int n:layer.history)out.put(n);}
            for(const auto& play:body.playbacks){for(int n:{play.state,play.frame,play.mode,play.desiredMode,play.step,play.parent,play.child})out.put(n);out.flag(play.active);out.flag(play.reusable);}
        }return out.bytes;};
        const auto before=motion();const auto sourceTick=world.sourceTick;const int frame=vm.staticField(393).integer(),millis=vm.staticField(398).integer();
        const int guard=vm.staticField(395).integer();
        for(unsigned n=0;n<10;++n)tick();
        require(world.sourceTick==sourceTick+10&&vm.staticField(393).integer()==frame+10,"Main823/source scheduler did not advance ten frames");
        require(motion()==before,"inventory advanced native actor timeline or local physics");
        require(vm.staticField(398).integer()==millis,"paused game-time advanced in inventory");
        require(vm.field(gui,837).integer()==2,"InterfaceInventory1168 did not service GuiSys1113 timer once per frame");
        require(vm.field(box,390).integer()==12,"GameTextBox811 ran outside original InterfaceGame1158/mode0");
        if(guard>0)require(vm.staticField(395).integer()==std::max(0,guard-10),"Main823 guard did not continue in inventory");
        const auto saved=world.saveStateBytes();world.restoreStateBytes(saved);require(world.saveStateBytes()==saved,"inventory frame/timer/global playback save did not restore exactly");
        // Native authored readers are covered by source-gui-text/presentation;
        // their rendering does not populate the retired modern document view.
        world.closeInventory();require(vm.staticField(844).integer()==0,"inventory Back did not restore mode0");tick();
        require(vm.field(box,390).integer()==11,"GameTextBox811 did not resume on the first gameplay frame");
        std::cout<<"Original inventory "<<(b?"B":"A")<<":10 Main/GUI frames, frozen actor timelines/local physics/gameclock, GuiSys12->2, GameTextBox held12 then11, exact save passed\n";
    }
    SDL_Quit();
}
