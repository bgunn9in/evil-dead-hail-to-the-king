#pragma once
// Unit geometry/pose fixtures on real World bodies. No story, HP or earned-item claim.
void testNativeMotionWorld(const fs::path& assets) {
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize native movement proof");
    auto require=[](bool yes,const char* message){if(!yes)throw std::runtime_error(std::string("Native zero motion: ")+message);};
    World world(assets,nullptr,presentMovieHeadless,true);testArmedLocomotionWorld(world);
    testChainsawControlsWorld(world);
    testSourceInputControlsWorld(world);
    auto& npc=world.actors->at({4,0});auto& ash=world.actors->at({0,0});
    for(const auto& [key,unused]:world.actors->bodies()){(void)unused;world.actors->at(key).flags=0;}
    world.scene.ground.clear();world.scene.queries.volumes.clear();world.scene.radius=.25f;
    ash.flags=1;ash.radius=80;ash.pose={0,0,0,0};npc.flags=1;npc.radius=30;npc.motionFlags=6;
    auto prepare=[&](float x,float z,std::array<float,3> delta){npc.pose={x,3.75f,z,0};npc.localY=.5f;npc.cachedFloorHeight=3.25f;npc.pendingMove=delta;npc.motionFlags2=0xff;};
    prepare(.4f,0,{});world.applyNativeActorMovement(npc);
    require(npc.pose.x==.4f&&npc.pose.z==0&&npc.localY==.5f&&npc.pose.y==3.75f&&npc.motionFlags2==0,"zero movement recovered an overlapping actor");
    prepare(.4f,0,{{0,.25f,0}});world.applyNativeActorMovement(npc);
    require(npc.pose.x==.4f&&npc.pose.z==0&&npc.localY==.75f&&npc.pose.y==4&&npc.cachedFloorHeight==3.25f&&npc.motionFlags2==0,"vertical-only movement changed XZ/contact/floor cache");
    prepare(1,0,{{-1,0,0}});world.applyNativeActorMovement(npc);
    require(npc.pose.x>.55f&&npc.pose.x<1&&(npc.motionFlags2&4),"positive horizontal actor sweep lost its existing contact4 path");
    ash.flags=0;
    GroundPolygon solid;solid.points={{-1,-1},{1,-1},{1,1},{-1,1}};solid.sourceIndex=0;world.scene.ground={solid};
    ScriptVolume shape;shape.kind=2;shape.shape=0;shape.points={{-1,0,-1},{1,0,1}};world.scene.queries.volumes={{shape,0,0xe,0}};
    prepare(0,0,{});world.applyNativeActorMovement(npc);
    require(npc.pose.x==0&&npc.pose.z==0&&npc.motionFlags2==0,"zero movement recovered a static-solid overlap");
    prepare(0,0,{{0,.25f,0}});world.applyNativeActorMovement(npc);
    require(npc.pose.x==0&&npc.pose.z==0&&npc.localY==.75f&&npc.pose.y==4&&npc.motionFlags2==0,"vertical-only movement queried static collision");
    prepare(-2,0,{{3,0,0}});world.applyNativeActorMovement(npc);
    require(std::abs(npc.pose.x+2.3f)<.00001f&&npc.pose.z==0&&npc.motionFlags2==0,
        "native static response did not retry the full delta/reset contacts after a free retry");
    GroundPolygon retryWall;retryWall.points={{-4,-1},{-2.2f,-1},{-2.2f,1},{-4,1}};retryWall.sourceIndex=0;
    world.scene.ground.push_back(retryWall);prepare(-2,0,{{3,.25f,0}});world.applyNativeActorMovement(npc);
    require(npc.pose.x==-2&&npc.pose.z==0&&npc.localY==.5f&&npc.motionFlags2==2,
        "blocked native retry advanced position or vertical motion");
    world.scene.ground.pop_back();prepare(-2,0,{{3,.005f,0}});world.applyNativeActorMovement(npc);
    require(npc.localY==.5f&&npc.motionFlags2==0,"native contact retry lost its one-centimetre snap");
    ash.flags=1;ash.pose.x=-2.8f;ash.pose.z=0;prepare(-2,0,{{3,.25f,0}});world.applyNativeActorMovement(npc);
    require(npc.pose.x==-2&&npc.pose.z==0&&npc.localY==.5f&&npc.motionFlags2==4,
        "native static retry skipped an actor or retained the first static contact flag");
    ash.flags=0;
    ash.pose={0,5,0,0};ash.localY=2;ash.cachedFloorHeight=3;ash.pendingMove={};ash.motionFlags2=6;world.applyNativeActorMovement(ash);
    require(ash.pose.x==0&&ash.pose.z==0&&ash.pose.y==5&&ash.motionFlags2==0,"idle native Ash pose was forcibly recovered");
    {
        // BM02 original static volumes0/1 and the real failed-frame NPC circle.
        // This is a bounded geometry fixture, not a recreated gameplay/HP state.
        Scene scene;scene.radius=.25f;scene.speed=1;scene.cameras.push_back({});
        scene.cameras[0].right={1,0};scene.cameras[0].forward={0,1};
        scene.ground={
            {false,{{-9.572133064f,9.149703026f},{-16.20213127f,21.80973244f},{-15.78211975f,23.74973106f},{-9.042125702f,11.2297287f}}},
            {false,{{-10.80454731f,8.004504204f},{-24.08456802f,25.03454971f},{-16.40455055f,21.35450745f},{-9.854492188f,8.304459572f}}}
        };
        scene.floor={{{-30,0,0},{0,0,0},{0,0,30}},{{-30,0,0},{0,0,30},{-30,0,30}}};
        GroundPolygon circle;circle.points={{-14.00688553f,17.61787605f}};circle.circleRadius=.2999999821f;
        const std::vector<GroundPolygon> dynamic{circle};const Vec2 start{-14.42324924f,17.91845322f};
        const Vec2 delta{-.02203182317f,.08292309195f};const float radius=.2294063568f;
        auto combined=scene.ground;combined.push_back(circle);
        require(clearGround(scene.ground,start,radius)&&!clearGround(combined,start,radius),"BM02 fixture lost its static-safe/dynamic-overlap precondition");
        require(!clearGround(scene.ground,moveGround(combined,start,delta,radius),radius),"BM02 fixture no longer exercises failed combined recovery");
        Actor player;player.position[0]=start.x;player.position[2]=start.z;player.clearanceRadius=radius;
        const auto step=stepActor(scene,player,delta,false,1,true,dynamic);
        auto inflated=scene.ground;inflated.push_back(circle);inflated.back().circleRadius+=scene.radius;
        require(player.clearanceRadius==0&&step.requested>0&&clearGround(scene.ground,{player.position[0],player.position[2]},0)&&
            clearGround(inflated,{player.position[0],player.position[2]},0),"source point movement lost static safety or nominal NPC separation");
        bool rejected=false;
        try{Actor legacy;legacy.position[0]=start.x;legacy.position[2]=start.z;legacy.clearanceRadius=radius;stepActor(scene,legacy,delta,false,1,false,dynamic);}
        catch(const std::runtime_error& e){rejected=std::string(e.what())=="Collision resolver left the walkable floor";}
        require(rejected,"source-only dynamic fallback suppressed the legacy static guard");
        rejected=false;
        try{Scene staticOnly=scene;staticOnly.ground=combined;Actor invalid;invalid.position[0]=start.x;invalid.position[2]=start.z;invalid.clearanceRadius=radius;stepActor(staticOnly,invalid,delta,false,1,false);}
        catch(const std::runtime_error& e){rejected=std::string(e.what())=="Collision resolver left the walkable floor";}
        require(rejected,"legacy static-only recovery guard was suppressed");
        const Vec2 invalidOld=recoverGround(combined,start,radius);
        require(!clearGround(scene.ground,invalidOld,radius),"invalid-old fixture became statically safe");
        rejected=false;
        try{Actor invalid;invalid.position[0]=invalidOld.x;invalid.position[2]=invalidOld.z;invalid.clearanceRadius=radius;stepActor(scene,invalid,delta,false,1,false,dynamic);}
        catch(const std::runtime_error& e){rejected=std::string(e.what())=="Collision resolver left the walkable floor";}
        require(rejected,"legacy invalid-old recovery guard was suppressed");
        // A point can still have no feasible static+dynamic solution. Keep the
        // last static-safe point instead of recovering it outside the room.
        Scene closed;closed.radius=.25f;closed.speed=1;closed.cameras=scene.cameras;
        closed.ground={{true,{{-1,-1},{1,-1},{1,1},{-1,1}}}};
        closed.floor={{{-1,0,-1},{1,0,-1},{1,0,1}},{{-1,0,-1},{1,0,1},{-1,0,1}}};
        GroundPolygon engulfing;engulfing.points={{0,0}};engulfing.circleRadius=2;
        Actor blocked;blocked.position[2]=.5f;blocked.clearanceRadius=.1f;
        const auto blockedStep=stepActor(closed,blocked,{.3f,0},false,1,true,{engulfing});
        require(blocked.position[0]==0&&blocked.position[2]==.5f&&blocked.clearanceRadius==0&&blockedStep.moved==0&&
            blockedStep.requested>0&&blocked.animationTime==1,"infeasible dynamic recovery left the static-safe point or stopped its clock");
        std::cout<<"BM02: quarter-capsule failure certificate/legacy guards retained; source static-point/NPC separation and infeasible-circle fallback passed\n";
    }
    std::cout<<"Native413633 zero-XZ: real World NPC/Ash, actor+solid overlap, vertical/localY/cache, contact reset and unchanged moving2/4 sweeps passed\n";
    SDL_Quit();
}
