#include "source_inventory_ui.inl"
void continueEpisodeWalkWorld(World& world,bool draw,int width,int height,const fs::path& capture,const std::function<void(World&)>& continueStory={}) {
    const auto& root=world.root;const auto transitionBase=world.transitions;
    std::unique_ptr<Graphics> graphics;
    bool exactDisplayProof=false;
    if(draw) {
        graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
        graphics->loadFont(root);graphics->loadInventory(root);graphics->loadActorTextures(root,*world.actors);
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if(mode&&width==mode->w&&height==mode->h) {
            for(unsigned i=0;i<3;++i){SDL_PumpEvents();graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false);}
            graphics->setFullscreen(true);
            exactDisplayProof=true;
        }
    }
    auto render=[&](const std::string& suffix){if(!graphics)return;
        bool rendered=false;for(unsigned i=0;i<30&&!rendered;++i){SDL_PumpEvents();rendered=graphics->render(world.scene,world.actor.camera,
            world.actor.position,world.actor.yaw,world.actor.animationTime,world.actor.walking,world.hud(),world.inventoryView.open?&world.inventoryView:nullptr,
            world.actors.get(),world.drawObjects(),world.drawEffects(),world.drawActorTints(),world.lastOverlay,world.lastProjectionShake)&&
            (!exactDisplayProof||(graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));}
        if(!rendered)throw std::runtime_error("Original actor registry frame was unavailable");
        if(!capture.empty())graphics->screenshot(suffix.empty()?capture:capture.parent_path()/(capture.stem().string()+suffix+capture.extension().string()));
    };
    render("-startup");
    auto require=[&](bool value,const std::string& text){if(!value)throw std::runtime_error("Original episode: "+text);};
    auto controlled=[&](const ControlFrame& input,bool run,bool use,float seconds){
        if(world.game.profile.dreamcast&&use&&seconds==0)seconds=1.0f/30;
        if(world.game.profile.dreamcast&&seconds>0){useSourceHealingItem(world);useSourceOwnedFuelItem(world);}
        if(!world.game.profile.dreamcast||seconds>0)world.playerControls(input);
        require(world.notice!="could not perform action","source player action failed; see native diagnostic");
        world.step(input.movement,run,use,seconds,graphics.get());if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);
        require(world.game.vm.field(world.game.ash,13).integer()>0,"Ash died while following the source episode");
    };
    auto tick=[&](Vec2 stick=Vec2{},bool run=false,bool use=false,float seconds=1.0f/30){
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Run)]=run;controlled(input,run,use,seconds);
    };
    auto back=[&](){if(world.inventoryView.open){render("-inventory");world.closeInventory();require(!world.inventoryView.open,"source inventory Back failed");}};
    auto playerState=[&](){const auto& body=world.actors->at({0,0});const auto& layer=body.actions.layers[0];
        std::string result="tick="+std::to_string(world.sourceTick)+" mode="+std::to_string(world.game.mode)+
            " pause48="+std::to_string(world.game.vm.staticField(48).integer())+" flags="+std::to_string(body.flags)+" state="+std::to_string(layer.state)+
            " frame="+(layer.playback<0?std::string("none"):std::to_string(body.actions.playbacks.at(size_t(layer.playback)).frame))+
            " busy="+std::to_string(world.game.vm.field(world.game.ash,11).integer())+
            " aim="+std::to_string(world.game.vm.field(world.game.ash,123).integer())+
            " guard="+std::to_string(world.guard)+" inputLatch="+std::to_string(world.game.vm.staticField(71).integer())+
            " walk3="+std::to_string(body.actions.possible(3))+" lower23="+std::to_string(body.actions.possible(23))+
            " stop4="+std::to_string(body.actions.possible(4))+" run5="+std::to_string(body.actions.possible(5))+" stop6="+std::to_string(body.actions.possible(6))+
            " use116="+std::to_string(body.actions.possible(116))+" yaw="+std::to_string(world.actor.yaw);
        for(size_t i=0;i<body.actions.layers.size();++i){const auto& value=body.actions.layers[i];if(value.state>=0||value.playback>=0)
            result+=" L"+std::to_string(i)+"="+std::to_string(value.state)+"/"+std::to_string(value.playback);}
        for(size_t i=0;i<body.actions.playbacks.size();++i){const auto& value=body.actions.playbacks[i];if(value.active)
            result+=" P"+std::to_string(i)+"="+std::to_string(value.state)+",f"+std::to_string(value.frame)+",m"+
                std::to_string(value.mode)+",parent"+std::to_string(value.parent)+",child"+std::to_string(value.child);}
        return result;};
    auto awaitNeutral=[&](){bool releaseAim=false;for(unsigned frame=0;frame<300;++frame){back();const auto& body=world.actors->at({0,0});
            if(!world.guard&&!world.game.vm.field(world.game.ash,11).integer()&&!world.game.vm.field(world.game.ash,123).integer()&&
               !world.game.vm.staticField(71).integer()&&body.actions.possible(3)&&body.actions.possible(116)){
                if(frame)std::cout<<"Original input ready after "<<frame<<" neutral ticks: "<<playerState()<<"\n";return;}
            if(!frame)std::cout<<"Original input waiting: "<<playerState()<<"\n";
            if(releaseAim){tick();releaseAim=false;}
            else if(world.game.profile.dreamcast&&world.game.vm.field(world.game.ash,123).integer()&&body.actions.possible(23)){
                ControlFrame aim;aim.down[size_t(ControlAction::Aim)]=true;controlled(aim,false,false,1.0f/30);releaseAim=true;
            }else tick();}
        throw std::runtime_error("Original episode: source action did not finish: "+playerState());};
    back();
    auto stickToward=[&](Vec2 target,float magnitude){
        const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& move=world.actor.movement;
        const Vec2 right=move.held?move.right:camera.right,forward=move.held?move.forward:camera.forward;
        const Vec2 delta=target-Vec2{world.actor.position[0],world.actor.position[2]};const float determinant=right.x*forward.z-right.z*forward.x;
        require(std::abs(determinant)>.001f,"camera movement basis is singular");
        Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};
    };
    unsigned defeated=0,combatInputs=0;bool combatCaptured=false;
    auto combat=[&](){if(defeated&&!world.game.profile.dreamcast)return;const edactors::Body* victim=nullptr;float nearest=2.0f;
        for(const auto& [key,body]:world.actors->bodies())if(key.type==4&&(body.flags&1)&&!(body.flags&8)&&body.script&&world.game.vm.field(body.script,13).integer()>0){
            const float distance=length(Vec2{body.pose.x-world.actor.position[0],body.pose.z-world.actor.position[2]});if(distance<nearest){nearest=distance;victim=&body;}}
        if(!victim)return;const auto key=victim->key;const auto script=victim->script;const auto area=world.scene.id;
        for(unsigned frame=0;frame<1200;++frame){const auto& enemy=world.actors->at(key);const int health=world.game.vm.field(script,13).integer();
            if(health<=0){++defeated;std::cout<<"Original combat: Deadite "<<key.index<<" defeated at source tick "<<world.sourceTick<<"\n";break;}
            const Vec2 target{enemy.pose.x,enemy.pose.z};if(length(target-Vec2{world.actor.position[0],world.actor.position[2]})>3.0f)break;
            const bool native=world.game.profile.dreamcast;
            // Native Ash searches140cm beyond the target radius; approach with10cm of margin, then stand and swing.
            const bool approach=native&&length(target-Vec2{world.actor.position[0],world.actor.position[2]})>1.3f+float(enemy.radius)*.01f;
            if(native)tick(stickToward(target,1),false,false,0);
            ControlFrame input;input.movement=native?(approach?stickToward(target,1):Vec2{}):stickToward(target,.05f);
            input.down[size_t(ControlAction::Aim)]=!approach;
            const auto saw=world.game.vm.field(world.game.ash,102).reference();
            const bool powered=world.game.profile.dreamcast&&world.game.vm.field(saw,815).integer()>=2;
            input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=!approach&&frame%6==0;
            input.edges[size_t(ControlAction::ChainsawRev)]=!approach&&powered&&(world.game.vm.field(saw,139).integer()==0||world.game.vm.field(saw,139).integer()==2);
            combatInputs+=input.edges[size_t(ControlAction::PrimaryAttack)]||input.edges[size_t(ControlAction::SecondaryAttack)];
            controlled(input,false,false,1.0f/30);require(world.scene.id==area,"combat crossed a portal unexpectedly");
            const int after=world.game.vm.field(script,13).integer();
            if(graphics&&!combatCaptured&&after<health&&!world.drawEffects().empty()){
                render("-combat");combatCaptured=true;std::cout<<"Original combat GPU frame: source tick "<<world.sourceTick<<" HP "<<health<<" -> "<<after<<" with live particles\n";}
            if(frame==1199)throw std::runtime_error("Original Deadite combat did not finish");
        }awaitNeutral();tick({},false,false,0);
    };
    float arrivalTolerance=.04f;
    if(world.game.profile.dreamcast){
        const auto mesh=edmesh::ActorMesh::load(root/"actors/ash/actor.bin");
        const auto animation=world.actors->at({0,0}).actions.states.at(11).animation;
        float maximumStep=0;
        for(unsigned frame=1;frame<mesh.clip(animation).frameCount;++frame){
            const auto a=mesh.rootPosition(animation,frame-1),b=mesh.rootPosition(animation,frame);
            maximumStep=std::max(maximumStep,std::hypot(b[0]-a[0],b[2]-a[2]));
        }
        // Native movement ignores stick magnitude; stop within half an authored walk step.
        arrivalTolerance=std::max(arrivalTolerance,maximumStep*.5f+.0001f);
        require(maximumStep>0&&arrivalTolerance<.05f,"source walking step exceeds checked waypoint clearance");
        std::cout<<"Original walking maximum step="<<maximumStep<<" arrival tolerance="<<arrivalTolerance<<"\n";
    }
    auto walk=[&](Vec2 target,const std::string& area,const std::string& destination=std::string{}){
        tick({},false,false,0);unsigned stalled=0;float previous=std::numeric_limits<float>::max();
        for(unsigned frame=0;frame<9000;++frame){if(world.scene.id!=area){require(world.scene.id==destination,"unexpected transition while walking in "+area);return;}
            back();combat();
            const float remaining=length(target-Vec2{world.actor.position[0],world.actor.position[2]});if(remaining<arrivalTolerance)break;
            if(remaining>=previous-.00001f)++stalled;else stalled=0;require(stalled<120,"walking stalled in "+area+" at "+std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[2]));
            previous=remaining;
            const bool native=world.game.profile.dreamcast;
            const auto& actions=world.actors->at({0,0}).actions;
            // A pickup/hit can finish idle while124 still remembers held Forward; release before pressing again.
            if(native&&world.game.vm.field(world.game.ash,124).integer()&&!edlocomotion::manualAction(actions)&&actions.possible(3)&&
               !world.guard&&!world.game.vm.field(world.game.ash,11).integer())tick();
            else tick(stickToward(target,native?1.0f:std::min(1.0f,remaining/(world.scene.speed*2.0f/30))),!native||remaining>.5f);
            if(frame==8999)throw std::runtime_error("Original episode route exceeded its frame budget");
        }tick({},false,false,0);
    };
    auto faceMarker=[&](unsigned index){awaitNeutral();const auto p=world.scene.scripting.markers.at(index);
        tick({},false,false,0);tick(stickToward({p.x,p.z},1),false,false,0);
        require(world.scene.scripting.facing(index,45,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),
            "source facing-marker input was blocked: "+playerState());};
    auto nav=[&](int index){auto u32=[&](size_t offset){uint32_t value;std::memcpy(&value,world.scene.nativeBytes.data()+offset,4);return value;};
        const size_t count=u32(48),offset=u32(52);require(index>=0&&size_t(index)<count,"source route nav index is missing");
        GroundPoint point;std::memcpy(&point,world.scene.nativeBytes.data()+offset+size_t(index)*44+8,12);return Vec2{point.x,point.z};};
    walk({2.90000248f,.96540868f},"ak01");walk({1.30000317f,-.33459133f},"ak01");
    // Authored nav0 is inside74; the former edge target had less than half a walk step of margin.
    walk(world.game.profile.dreamcast?nav(0):Vec2{.0793162f,-1.9016995f},"ak01");
    require(world.actor.clearanceRadius==0,"source movement did not use original static-point collision");
    faceMarker(32);for(unsigned i=0;i<60&&world.guard;++i)tick();
    tick({},false,true,0);require(world.pending>=0,"source cabin door did not accept Use: "+playerState()+
        " zone74="+std::to_string(world.scene.scripting.inside(74,{world.actor.position[0],world.actor.position[1],world.actor.position[2]})));
    for(unsigned i=0;i<90&&world.scene.id=="ak01";++i)tick();
    require(world.scene.id=="ak09"&&world.transitions==transitionBase+1,"source cabin portal2 did not transfer to AK09");
    std::cout<<"Original episode checkpoint: walked cabin portal2 passed\n";
    struct Point {int nav;Vec2 position;};struct Leg {const char* scene;const char* destination;int portal;std::vector<Point> points;};
    // Test input follows the independently checked original nav coordinates and
    // collision-derived player-only waypoints in research/episode-walking-route.json.
    const std::vector<Leg> legs{
        {"ak09","al01",11,{{43,{}},{50,{}},{5,{}},{7,{}},{12,{}},{-1,{-.115413666f,22.5304737f}}}},
        // Original portal6 ends at X=-6.326952. The former X=-6.365077
        // target was only.038125 inside, less than the walking stop distance.
        // X=-6.4 retains.315725 source static clearance along the final segment.
        {"al01","al03",6,{{-1,{9.93685226f,40.4f}},{-1,{7.93685226f,42}},{-1,{-6.4f,41.8740091f}}}},
        {"al03","",-1,{{0,{}},{8,{}},{9,{}},{12,{}},{14,{}},{27,{}},{28,{}},{31,{}},{33,{}},{38,{}},{43,{}},{44,{}},{-1,{-28.6501007f,78.3878021f}}}},
        {"al03","al01",4,{{44,{}},{43,{}},{38,{}},{33,{}},{31,{}},{28,{}},{27,{}},{14,{}},{12,{}},{9,{}},{8,{}},{0,{}},{-1,{8.80991471f,42.0301971f}}}},
        {"al01","ak09",5,{{-1,{10.5468187f,22.5309505f}}}},
        {"ak09","ah50",6,{{7,{}},{5,{}},{50,{}},{-1,{-19.2529526f,-10.5664947f}}}},
        {"ah50","ah01",3,{{24,{}},{2,{}},{3,{}},{31,{}},{33,{}},{-1,{-11.2635319f,5.31365061f}}}},
        {"ah01","",-1,{{35,{}},{56,{}},{23,{}},{82,{}},{73,{}},{10,{}},{-1,{-16.2990556f,1.06468588f}}}},
        {"ah01","ah02",3,{{-1,{-16.5234203f,1.03337336f}}}}
    };
    auto quantity=[&](int item){int value=0;for(const auto& held:world.game.items())if(held.itemID==item)value+=held.quantity;return value;};
    auto passageState=[&](const Leg& leg,Vec2 goal,const char* phase){
        const Vec2 position{world.actor.position[0],world.actor.position[2]};const auto mask=triggerMask(world.scene.triggerGrid,position);
        std::string detail="phase="+std::string(phase)+" scene="+world.scene.id+" portal="+std::to_string(leg.portal)+
            " xyz="+std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[1])+","+std::to_string(world.actor.position[2])+
            " remaining="+std::to_string(length(goal-position))+" pointMask="+std::to_string(mask)+
            " attempts="+std::to_string(world.attempts)+" pending="+std::to_string(world.pending)+
            " failedPortal="+std::to_string(world.failedPortal)+" notice="+world.notice;
        if(leg.portal>=0&&size_t(leg.portal)<world.scene.nativePortals.size()){
            const auto& portal=world.scene.nativePortals[size_t(leg.portal)];const auto match=mask&portal.mask;
            detail+=" nativeInside="+std::to_string(insideTrigger(portal,position))+" portalMask="+std::to_string(portal.mask)+
                " lowMatch="+std::to_string(match&65535u)+" highMatch="+std::to_string(match&0xffff0000u)+
                " rawEnabled="+std::to_string(world.scene.enabledPortals.at(size_t(leg.portal)));
            bool found=false;for(size_t i=0;i<world.scene.doors.size();++i)if(world.scene.doors[i].portalIndex==unsigned(leg.portal)){
                const auto& gate=world.gates.at(i);found=true;detail+=" gateIndex="+std::to_string(i)+
                    " gateEnabled="+std::to_string(gate.enabled)+" gatePending="+std::to_string(gate.pending)+
                    " gateElapsed="+std::to_string(gate.elapsed)+" exportedInside="+
                    std::to_string(insideDoorRegion(world.scene.doors[i].portal,world.scene.triggerGrid,position));}
            if(!found)detail+=" gate=missing";
        }
        detail+=" Main309="+std::to_string(world.game.vm.staticField(309).integer())+
            " mode="+std::to_string(world.game.mode)+" scriptPortal="+std::to_string(world.game.vm.staticField(world.game.hostField(HostFieldRole::ScenePortalIndex)).integer())+
            " scriptPending="+std::to_string(world.game.vm.staticField(world.game.hostField(HostFieldRole::ScenePendingPortal)).integer())+
            " scriptCountdown1362="+std::to_string(world.game.vm.staticField(1362).integer());
        if(world.scene.id=="al01")detail+=" AL01activate2054(false):PC239,noPortalEnableOrDisable;Main400="+
            std::to_string(world.game.vm.staticField(400).integer())+" scene1386="+
            std::to_string(world.game.vm.field(world.game.scene,1386).integer())+" scene1387="+
            std::to_string(world.game.vm.field(world.game.scene,1387).integer());
        std::cout<<"Original passage diagnostic: "<<detail<<"\n";return detail;
    };
    struct SaveScratch {fs::path path;explicit SaveScratch(fs::path parent):path(parent/("episode-proof-"+std::to_string(SDL_GetTicksNS()))){
        if(!fs::create_directory(path))throw std::runtime_error("Cannot create owned episode save directory");}
        ~SaveScratch(){std::error_code ignored;fs::remove(path/"quicksave.eds",ignored);fs::remove(path,ignored);}} scratch(root.parent_path());
    auto saveRoundtrip=[&](){back();require(world.saveGame(scratch.path),"real episode save failed");const auto saved=savefile::read(scratch.path,"quicksave.eds");
        const auto tickAtSave=world.sourceTick;for(unsigned i=0;i<4;++i)tick();require(world.loadGame(scratch.path,graphics.get()),"real episode load failed");
        require(world.sourceTick==tickAtSave&&world.saveStateBytes()==saved,"episode VM/actors/routes/audio did not restore exactly");};
    for(size_t legIndex=0;legIndex<legs.size();++legIndex){const auto& leg=legs[legIndex];require(world.scene.id==leg.scene,"source episode leg starts in another area");
        std::cout<<"Original episode walking "<<leg.scene<<" leg "<<legIndex<<" at tick "<<world.sourceTick<<"\n";
        for(size_t i=0;i<leg.points.size();++i){const auto& point=leg.points[i];const Vec2 target=point.nav<0?point.position:nav(point.nav);
            if(leg.portal>=0&&i+1==leg.points.size())passageState(leg,target,"before-final-approach");
            walk(target,leg.scene,i+1==leg.points.size()?leg.destination:"");}
        if(leg.portal>=0){
            const auto& last=leg.points.back();const Vec2 goal=last.nav<0?last.position:nav(last.nav);
            if(world.scene.id==leg.scene)passageState(leg,goal,"endpoint-before-wait");
            for(unsigned i=0;i<180&&world.scene.id==leg.scene;++i)tick();
            if(world.scene.id!=leg.destination){const auto detail=passageState(leg,goal,"failed-after-wait");
                require(false,"source passage did not transfer on leg "+std::to_string(legIndex)+": "+detail);}}
        if(legIndex==2){require(quantity(29)==0&&world.game.vm.staticField(432).integer()==0,"cord was already present before original pickup");
            tick({},false,true,0);for(unsigned i=0;i<180&&!quantity(29);++i)tick();
            require(quantity(29)==1&&world.game.vm.staticField(432).integer()==1,"original AL03 pickup did not add cord29/set432");render("-cord");saveRoundtrip();}
        if(legIndex==7){require(quantity(29)==1&&world.game.vm.staticField(435).integer()==0,"source machine state changed before item use");
            faceMarker(23);for(unsigned i=0;i<90&&world.guard;++i)tick();world.openInventory();world.syncInventory();
            const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& value){return value.itemID==29;});
            require(item!=world.inventoryView.items.end(),"source cord slot is missing");world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());render("-use-cord");world.applyInventoryItem();
            require(quantity(29)==0&&world.game.vm.staticField(435).integer()==1&&world.pending>=0,"original AH01 item callback did not consume29/unlock its passage");}
    }
    require(world.scene.id=="ah02"&&world.transitions==transitionBase+8,"eight original scene transitions did not complete");
    require(defeated>0&&combatInputs>0,"episode route did not exercise a real source-spawned Deadite combat");
    require(!graphics||combatCaptured,"GPU combat proof did not capture an original hit with live particles");saveRoundtrip();render("");
    graphics.reset();if(continueStory)continueStory(world);
}

void testEpisode(const fs::path& root,bool draw,int width,int height,const fs::path& capture,const std::function<void(World&)>& continueStory={}) {
    std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original episode proof");
    World world(root,nullptr,presentMovieHeadless,true);
    if(world.actors->bodies().size()!=9||world.game.vm.field(world.game.ash,6).integer()!=0)
        throw std::runtime_error("Original startup did not create its Ash and two four-actor pools");
    const auto marker=world.scene.scripting.markers.at(4);
    if(world.actor.camera!=2||world.actor.position[0]!=marker.x||world.actor.position[2]!=marker.z)
        throw std::runtime_error("Original new-game camera/marker differs from source activation");
    const auto& initial=world.actors->at({0,0});
    const auto initialPlayback=initial.actions.playbacks.at(size_t(initial.actions.layers[0].playback));
    const auto& initialState=initial.actions.states.at(size_t(initialPlayback.state));
    if(initialPlayback.state!=0||initialPlayback.frame!=0||initialState.animation!=0x01024084)
        throw std::runtime_error("Original startup is not Ash's source idle frame0");
    const auto mesh=edmesh::ActorMesh::load(root/"actors/ash/actor.bin");
    const auto a=mesh.rootPosition(initialState.animation,0),b=mesh.rootPosition(initialState.animation,1);
    const auto idle=edactormath::pendingToWorld({{a[0]-b[0],0,b[2]-a[2]}},{{0,initial.pose.yaw,0}},initial.motionFlags);
    const Vec2 expected{marker.x+idle[0],marker.z+idle[2]};
    world.step({},false,false,1.0f/30);
    if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);
    if(world.actor.position[0]!=expected.x||world.actor.position[2]!=expected.z)
        throw std::runtime_error("Original first-frame displacement: "+std::to_string(world.actor.position[0]-marker.x)+","+
            std::to_string(world.actor.position[2]-marker.z)+"; source idle expected "+std::to_string(idle[0])+","+std::to_string(idle[2]));
    const auto pacedState=world.saveStateBytes();const auto pacedTick=world.sourceTick;
    world.step({},false,false,.25f);
    if(world.scriptFault||world.sourceTick!=pacedTick+1)throw std::runtime_error("Original slow frame performed catch-up iterations");
    world.restoreStateBytes(pacedState);world.step({},false,false,0);
    if(world.sourceTick!=pacedTick)throw std::runtime_error("Paused source frame advanced the native iteration");
    continueEpisodeWalkWorld(world,draw,width,height,capture,continueStory);SDL_Quit();
    std::cout<<"Original episode passed: Main815 startup, source idle root-motion, eight walked source passages, cord29 pickup/use, original Deadite combat and exact file save/load\n";
}
