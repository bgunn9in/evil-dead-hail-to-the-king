#pragma once

void continueAH02WalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;const auto arrivalTransitions=world.transitions;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original AH02 walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ah02"&&vm.staticField(435).integer()==1&&!quantity(29),"did not receive the same earned eight-passage arrival");
    require(!vm.staticField(438).integer()&&!quantity(170)&&!vm.staticField(440).integer()&&!quantity(30),"source pickups were already earned");
    const Vec2 arrival{world.actor.position[0],world.actor.position[2]};unsigned frames=0;float distance=0;
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f){const Vec2 old{world.actor.position[0],world.actor.position[2]};
        ControlFrame input;input.movement=stick;world.playerControls(input);const auto moved=world.step(stick,false,use,dt);
        require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);require(world.scene.id=="ah02","walking left AH02 unexpectedly");
        require(vm.field(world.game.ash,13).integer()>0,"Ash died on the source pickup route");
        const Vec2 current{world.actor.position[0],world.actor.position[2]};
        require(floorHeight(world.scene.floor,current).has_value()&&clearGround(world.scene.ground,current,world.actor.clearanceRadius),"walking left decoded floor or penetrated an active source solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"walking exceeded requested displacement");distance+=length(current-old);frames+=dt>0;
    };
    auto ready=[&](){for(unsigned n=0;n<360;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original AH02 walk: source action did not become ready");};
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera movement basis is singular");
        Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};const float size=length(stick);
        return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto walk=[&](Vec2 target){tick({},false,0);unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<1800;++n){const float remaining=length(target-Vec2{world.actor.position[0],world.actor.position[2]});if(remaining<.04f){tick({},false,0);return;}
            stalled=remaining>=previous-.00001f?stalled+1:0;require(stalled<120,"route stalled near "+std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[2])+" toward "+std::to_string(target.x)+","+std::to_string(target.z));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*.033f))));}
        throw std::runtime_error("Original AH02 walk: route exceeded its bounded input budget");};
    auto nav=[&](unsigned index){auto u32=[&](size_t offset){uint32_t value;std::memcpy(&value,world.scene.nativeBytes.data()+offset,4);return value;};
        const size_t count=u32(48),offset=u32(52);require(index<count,"source nav point is missing");GroundPoint p;std::memcpy(&p,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    auto face=[&](unsigned marker){ready();const auto p=world.scene.scripting.markers.at(marker);tick({},false,0);tick(toward({p.x,p.z},1),false,0);
        require(world.scene.scripting.facing(marker,45,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"real direction input did not face the source marker");};
    auto roundtrip=[&](){ready();const auto saved=world.saveStateBytes();for(unsigned n=0;n<4;++n)tick();world.restoreStateBytes(saved,nullptr);
        require(world.saveStateBytes()==saved,"earned pickups/source cameras/full state did not restore byte-exactly");};
    // Fixed original nav chain from the actual AH01 portal arrival; no Ash pose setters.
    for(unsigned index:{13,12,9,8,7,6})walk(nav(index));
    walk({-15.85f,15.2f});face(31);
    require(world.scene.scripting.inside(48,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]}),"walked moonshine approach missed original trigger48");
    tick({},true,0);for(unsigned n=0;n<240&&!quantity(170);++n)tick();
    require(quantity(170)==1&&vm.staticField(438).integer()==1,"real walked Use140 did not grant170/report438");roundtrip();
    require(world.transitions==arrivalTransitions&&vm.staticField(435).integer()==1&&!quantity(29)&&quantity(170)==1&&
        !vm.staticField(440).integer()&&!quantity(30),"walking changed the earlier cord or later handle quest state");
    std::cout<<"Original AH02 moonshine walk: arrival"<<arrival.x<<','<<arrival.z<<" -> source nav13/12/9/8/7/6 -> trigger48/facing31/Use140 ->170/438; "
        <<frames<<" source frames, "<<distance<<"m walked, camera"<<world.actor.camera<<", HP"<<vm.field(world.game.ash,13).integer()<<", exact full save\n";
}

void testAH02WalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);});
}
