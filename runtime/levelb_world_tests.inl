#pragma once
// Positional fixtures use original trigger/marker geometry; this is not a walking proof.
void continueCthulhuWorld(World& world,bool draw,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;
    const auto& root=world.root;
    auto require=[&](bool value,const std::string& text){if(!value)throw std::runtime_error("Original BC02: "+text);};
    require(world.levelBPackage&&world.scene.id=="bc02","requires the original LevelB/BC02 world");
    require(world.nativeProperties.objects.size()==7,"source activation did not create its seven real objects");
    auto& vm=world.game.vm;const auto& package=world.game.package;bool aiming=false;
    const unsigned bossField=world.game.profile.dreamcast?1479u:1547u,windowField=world.game.profile.dreamcast?1398u:1466u;
    const unsigned pillarFlagsField=world.game.profile.dreamcast?1496u:1564u,reportField=world.game.profile.dreamcast?1498u:1566u;
    const unsigned debounceField=world.game.profile.dreamcast?1489u:1557u,durationField=world.game.profile.dreamcast?1494u:1562u,openedField=world.game.profile.dreamcast?1495u:1563u;
    auto tick=[&](bool attack=false){ControlFrame input;input.down[size_t(ControlAction::Aim)]=aiming;input.edges[size_t(ControlAction::SecondaryAttack)]=attack;
        world.playerControls(input);require(world.notice!="could not perform action","player action failed: "+world.game.diagnostic);
        world.step({},false,false,1.0f/30);if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);
        require(world.scene.id=="bc02","source world left BC02 unexpectedly");require(vm.field(world.game.ash,13).integer()>0,"Ash died in the positional fixture");};
    for(unsigned frame=0;frame<180&&vm.field(world.game.scene,bossField).null();++frame)tick();
    const auto boss=vm.field(world.game.scene,bossField).reference();
    require(boss&&vm.objectClass(boss)==package.classIndex("Cthulhu"),"original wave did not create Cthulhu");
    const edactors::Key key{vm.field(boss,6).integer(),vm.field(boss,5).integer()};
    require(key.type==33&&vm.field(boss,13).integer()==100&&!vm.field(boss,windowField).integer(),"source boss defaults differ");
    std::unique_ptr<Graphics> graphics;bool exactDisplay=false;
    auto render=[&](World& active,const char* name){if(!draw)return;
        if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(active.scene,active.directory(),width,height);
            graphics->loadFont(active.root);graphics->loadInventory(active.root);graphics->loadActorTextures(active.root,*active.actors);}
        else graphics->commit(graphics->prepare(active.scene,active.directory()));
        auto frame=[&](){SDL_PumpEvents();return graphics->render(active.scene,active.actor.camera,active.actor.position,active.actor.yaw,
            active.actor.animationTime,active.actor.walking,active.hud(),nullptr,active.actors.get(),active.drawObjects(),active.drawEffects(),
            active.drawActorTints(),active.lastOverlay,active.lastProjectionShake,active.sourceTextPages,active.drawGuiSprites());};
        const auto* display=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if(!exactDisplay&&display&&width==display->w&&height==display->h){for(unsigned n=0;n<3;++n)frame();
            graphics->setFullscreen(true);exactDisplay=true;}
        bool shown=false;for(unsigned n=0;n<30&&!shown;++n)shown=frame()&&(!exactDisplay||
            (graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));
        require(shown,"final boss GPU frame unavailable");
        if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+"-"+name+".bmp"));};
    render(world,"bc02-initial");
    auto neutral=[&](){aiming=false;unsigned ready=0;for(unsigned frame=0;frame<240;++frame){const auto& body=world.actors->at({0,0});
            if(!vm.field(world.game.ash,11).integer()&&!world.guard&&body.actions.possible(116)){if(++ready==3)return;}else ready=0;
            // Native auto-ready survives neutral input; an actual LB press/release requests Lower23.
            if(world.game.profile.dreamcast&&vm.field(world.game.ash,123).integer()&&!vm.field(world.game.ash,11).integer()&&
               !world.guard&&body.actions.possible(23)){aiming=true;tick();aiming=false;}
            tick();}
        throw std::runtime_error("Original BC02: Ash action did not finish; aim="+std::to_string(vm.field(world.game.ash,123).integer())+
            " state="+std::to_string(world.actors->at({0,0}).actions.layers[0].state));};
    auto place=[&](GroundPoint point,float yaw){auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{point.x,0,point.z});
        ash.pose.yaw=yaw;world.publishProgressBody(ash);world.actor.movement.held=false;
        world.actor.clearanceRadius=initialActorRadius(world.scene,{point.x,point.z});};
    auto pillarPoint=[&](unsigned trigger){const auto& points=world.scene.scripting.volumes.at(trigger).points;require(points.size()==2,"pillar fixture requires the original rectangle");
        GroundPoint selected{};float closest=std::numeric_limits<float>::max();const Vec2 center{(points[0].x+points[1].x)*.5f,(points[0].z+points[1].z)*.5f};
        for(unsigned x=1;x<16;++x)for(unsigned z=1;z<16;++z){const GroundPoint point{points[0].x+(points[1].x-points[0].x)*float(x)/16,0,
                points[0].z+(points[1].z-points[0].z)*float(z)/16};const Vec2 p{point.x,point.z};const float distance=length(p-center);
            if(distance<closest&&world.scene.scripting.inside(trigger,point)&&floorHeight(world.scene.floor,p)&&clearGround(world.scene.ground,p,world.scene.radius)){
                selected=point;closest=distance;}}
        require(closest<std::numeric_limits<float>::max(),"source pillar trigger has no walkable fixture sample");return selected;};
    auto flags=[&]()->const std::vector<jpc::Value>&{return vm.arrayValues(vm.field(world.game.scene,pillarFlagsField).reference());};
    auto hitPillar=[&](unsigned trigger,bool facing=true){neutral();const auto point=pillarPoint(trigger),marker=world.scene.scripting.markers.at(trigger+16);
        const float yaw=edactormath::modelYawFromHeading(float(std::atan2(double(marker.z)-point.z,double(marker.x)-point.x)));
        place(point,yaw+(facing?0:edactormath::Pi));aiming=true;tick(true);
        for(unsigned frame=0;frame<150&&(facing?flags().at(trigger-1).integer()!=0:frame<100);++frame)tick(frame%6==0);
        const auto& ash=world.actors->at({0,0});const auto& npc=world.actors->at(key);
        require(flags().at(trigger-1).integer()==(facing?0:1),facing?"real attack report did not break pillar "+std::to_string(trigger)+
            " xyz="+std::to_string(ash.pose.x)+","+std::to_string(ash.pose.z)+" yaw="+std::to_string(ash.pose.yaw)+" inside="+
            std::to_string(world.scene.scripting.inside(trigger,{ash.pose.x,ash.localY,ash.pose.z}))+" facing="+
            std::to_string(world.scene.scripting.facing(trigger+16,75,{ash.pose.x,ash.pose.z},ash.pose.yaw))+" playerState="+
            std::to_string(ash.actions.layers[0].state)+" busy="+std::to_string(vm.field(world.game.ash,11).integer())+
            " npcFlags="+std::to_string(npc.flags)+" npcThink="+std::to_string(npc.think)+" npcThread="+
            std::to_string(npc.think?int(vm.status(npc.think)):-1)+" reports="+std::to_string(vm.staticField(reportField).integer())+","+
            std::to_string(vm.staticField(reportField+1).integer())+","+std::to_string(vm.staticField(reportField+2).integer())+","+std::to_string(vm.staticField(reportField+3).integer())+
            " debounce="+std::to_string(vm.staticField(debounceField).integer()):"wrong-facing attack broke a pillar");};
    // Source medicine46 restores35HP; use the genuinely collected drop without overhealing.
    const int beforeHP=vm.field(world.game.ash,13).integer();int beforeCount=0;
    for(const auto& item:world.game.items(99))if(item.itemID==46)beforeCount+=item.quantity;
    if(beforeCount>0&&beforeHP<=vm.field(world.game.ash,95).integer()-35) {
        world.openInventory();world.syncInventory();const auto entry=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
            [](const auto& item){return item.containerField==99&&item.itemID==46;});
        require(entry!=world.inventoryView.items.end(),"owned medicine46 missing from original inventory UI");
        world.inventoryView.selected=unsigned(entry-world.inventoryView.items.begin());world.applyInventoryItem();
        int afterCount=0;for(const auto& item:world.game.items(99))if(item.itemID==46)afterCount+=item.quantity;
        require(afterCount==beforeCount-1&&vm.field(world.game.ash,13).integer()==beforeHP+35,"original medicine46 consumption/healing differs");
        world.closeInventory();std::cout<<"Earned source medicine46 UI: HP"<<beforeHP<<"->"<<vm.field(world.game.ash,13).integer()<<", quantity"<<beforeCount<<"->"<<afterCount<<'\n';
    }
    hitPillar(1,false);require(!vm.field(boss,windowField).integer(),"negative fixture opened vulnerability");
    auto open=[&](){for(unsigned pillar=1;pillar<=4;++pillar)if(flags().at(pillar-1).integer())hitPillar(pillar);
        for(unsigned frame=0;frame<4&&!vm.field(boss,windowField).integer();++frame)tick();
        require(vm.field(boss,windowField).integer()==1&&vm.staticField(durationField).integer()==-7500,"four source pillars did not open the 7500ms window");};
    auto damage=[&](){neutral();const auto& target=world.actors->at(key);const GroundPoint point{target.pose.x+1.4f,0,target.pose.z};
        require(clearGround(world.scene.ground,{point.x,point.z},0),"source boss hit fixture is blocked");place(point,-edactormath::HalfPi);aiming=true;
        require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==10,"source B startup no longer equips the expected left weapon10");
        const int health=vm.field(boss,13).integer();for(unsigned frame=0;frame<150&&vm.field(boss,13).integer()==health;++frame)tick(frame%6==0);
        require(vm.field(boss,13).integer()==health-8,"original input17/contact/hit handler expected8HP, before="+std::to_string(health)+
            " after="+std::to_string(vm.field(boss,13).integer())+" window="+std::to_string(vm.field(boss,windowField).integer())+
            " leftWeapon="+std::to_string(vm.field(vm.field(world.game.ash,103).reference(),812).integer())+
            " npcState="+std::to_string(world.actors->at(key).actions.layers[0].state)+" playerState="+
            std::to_string(world.actors->at({0,0}).actions.layers[0].state)+" xyz="+
            std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[2]));};
    open();damage();
    struct Scratch {fs::path path;explicit Scratch(const fs::path& parent):path(parent/("levelb-proof-"+std::to_string(SDL_GetTicksNS()))){
        if(!fs::create_directory(path))throw std::runtime_error("Cannot create LevelB save directory");}
        ~Scratch(){std::error_code ignored;fs::remove(path/"quicksave.eds",ignored);fs::remove(path,ignored);}} scratch(root.parent_path());
    world.closeInventory();require(world.saveGame(scratch.path),"actual saveGame failed");const auto saved=world.saveStateBytes();
    for(unsigned frame=0;frame<4;++frame)tick();require(world.loadGame(scratch.path,nullptr),"actual loadGame failed");
    require(world.saveStateBytes()==saved&&vm.field(boss,13).integer()==92&&vm.field(boss,windowField).integer(),"pending source world save did not restore exactly");
    render(world,"bc02-vulnerable");
    place(pillarPoint(1),0);const int opened=vm.staticField(openedField).integer();
    for(unsigned frame=0;frame<260&&vm.field(boss,windowField).integer();++frame)tick();
    require(!vm.field(boss,windowField).integer()&&vm.staticField(398).integer()-opened>7500,"source vulnerability did not close strictly after7500ms");
    for(const auto& flag:flags())require(flag.integer()==1,"source timer did not reset all four pillars");
    open();damage();require(vm.field(boss,13).integer()==84,"repeated source puzzle did not preserve real damage");
    const auto repeated=world.saveStateBytes();tick();world.restoreStateBytes(repeated,nullptr);require(world.saveStateBytes()==repeated,"repeated window restore changed state");
    unsigned hits=2;
    for(unsigned attempt=0;attempt<20&&vm.field(boss,13).integer()>0;++attempt) {
        // Keep enough of the source7500ms window for the ordinary aim/hurt
        // actions; expired windows are reopened by four genuine attack reports.
        if(vm.staticField(398).integer()-vm.staticField(openedField).integer()>3500) {
            place(pillarPoint(1),0);for(unsigned n=0;n<260&&vm.field(boss,windowField).integer();++n)tick();
        }
        if(!vm.field(boss,windowField).integer())open();
        damage();++hits;require(vm.field(boss,13).integer()==100-int(hits)*8,"source damage sequence changed");
    }
    require(hits==13&&vm.field(boss,13).integer()==-4,"thirteen real8HP hits did not finish the source boss");
    render(world,"bc02-defeated");
    const size_t movieCount=world.nativeProperties.movies.size();tick();
    require(world.nativeProperties.movies.size()==movieCount+2,"source ending did not play both movies");
    const auto& first=world.nativeProperties.movies[movieCount];const auto& second=world.nativeProperties.movies[movieCount+1];
    require(first.scene=="bc02"&&first.resource==0x62313800&&first.flags==2&&first.played&&
        second.scene=="bc02"&&second.resource==0x62313900&&second.flags==0&&second.played,"ending movie IDs/flags/order differ from SceneBC02");
    require(world.pendingSceneSwitch&&world.pendingSceneSwitch->sourceSceneId==0x62630200&&world.pendingSceneSwitch->destinationId==0x62620100&&
        world.pendingSceneSwitch->camera==0&&world.pendingSceneSwitch->marker==-1,"actual reboot did not queue BB01/camera0/marker-1");
    require(vm.staticField(401).integer()==0,"source ending did not clear its HUD/boss mode");
    const auto reboot=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();const auto oldEnding=world.saveStateBytes();
    auto title=stagePackageWorld(world,reboot);
    require(title->scene.id=="bb01"&&title->scene.menuContext&&title->rawClockSeconds==0&&world.saveStateBytes()==oldEnding,
        "actual ending reboot did not stage its fresh BB01 package/clock atomically");
    for(unsigned n=0;n<8&&!title->sourceMenuOwnsInput();++n){title->step({},false,false,.033f);
        require(!title->scriptFault,"ending BB01 startup failed: "+title->game.diagnostic);}
    require(title->sourceMenuOwnsInput()&&title->game.mode==1&&title->game.vm.staticField(844).integer()==1&&!title->sourceMenuText().empty(),
        "ending BB01 did not activate its genuine Main/Interface/menu");
    render(*title,"bb01-ending");
    std::cout<<"Original BC02 World: seven objects, real pillar reports,13x8HP -> -4,7500ms windows/save/repeat, ending movies B18/B19 and fresh BB01 Main/Interface passed; positional fixtures only\n";
    graphics.reset();
}
void testLevelBWorld(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original LevelB world proof");
    World world(root,nullptr,presentMovieHeadless,true);
    if(world.scene.id!="bc02") {
        world.endEpisodeScene();Scene next=loadScene(root/"bc02");Actor placed=resetActor(next,0);
        std::vector<GateState> gates(next.doors.size());world.enterSourceScene(next,placed,gates);
        world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(gates);
    }
    continueCthulhuWorld(world,draw,width,height,capture);SDL_Quit();
}
