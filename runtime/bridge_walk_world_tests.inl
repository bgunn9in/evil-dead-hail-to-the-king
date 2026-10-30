#pragma once
#include "levelb_story_world_tests.inl"

void testBridgeWalkWorld(const fs::path& levelA) {
    std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize original bridge walking proof");
    struct Quit{~Quit(){SDL_Quit();}} quit;
    World world(levelA/"levelB",nullptr,presentMovieHeadless,true);
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original bridge walk: "+text);};
    // One explicit Ash fixture per case, on the near-bank corridor between source
    // walls66/21 near nav1. Subsequent positions come only from ordinary input.
    const Vec2 start{-9.0f,7.8f},goal{-10.7f,10.2f};
    auto crossing=[&](bool opened) {
        require(world.scene.id=="bm01"&&bool(world.game.vm.staticField(531).integer())==opened&&world.scene.enabledVolumes.at(2)!=opened,
            "source bridge state does not match the case");
        require(floorHeight(world.scene.floor,start).has_value()&&clearGround(world.scene.ground,start,world.scene.radius),"near-bank fixture lacks capsule clearance");
        const auto& barrier=world.scene.scripting.volumes.at(2);
        require(!barrier.contains({start.x,0,start.z})&&!barrier.contains({goal.x,0,goal.z}),"route endpoints must be outside original blocker2");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{start.x,0,start.z});
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(goal.z)-start.z,double(goal.x)-start.x)));
        world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,start);world.actor.movement.held=false;
        require(std::abs(world.actor.clearanceRadius-world.scene.radius)<=ContactSkin,"near-bank fixture changed the full modern clearance radius");
        const Vec2 route=goal-start;const float distance=length(route);unsigned blocked=0,walked=0;float progress=0;bool crossedFormerBlocker=false,sawBridgeCamera=false;
        for(unsigned frame=0;frame<180;++frame) {
            require(world.game.vm.staticField(844).integer()==0,"walking unexpectedly opened a source menu");
            const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
            const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward;
            const Vec2 old{world.actor.position[0],world.actor.position[2]},delta=goal-old;
            crossedFormerBlocker|=opened&&barrier.contains({old.x,0,old.z});
            sawBridgeCamera|=world.actor.camera==1&&world.cameraTimeline&&world.cameraTimeline->state&&world.cameraTimeline->state->frame==1;
            const float determinant=right.x*forward.z-right.z*forward.x;
            require(std::abs(determinant)>.001f,"camera-relative movement basis is singular");
            Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
            const float magnitude=length(stick);if(magnitude>.000001f)stick=stick*(std::min(1.0f,length(delta)/(world.scene.speed*.033f))/magnitude);
            ControlFrame input;input.movement=stick;world.playerControls(input);const auto result=world.step(stick,false,false,.033f);
            require(!world.scriptFault,"source walking frame failed: "+world.game.diagnostic);
            require(world.game.vm.field(world.game.ash,13).integer()>0,"Ash died while following the noncombat bridge route");
            ++walked;
            if(world.scene.id!="bm01") {
                require(opened&&world.scene.id=="bm02","walking caused an unexpected scene transition");
                require(crossedFormerBlocker&&sawBridgeCamera,"opened route did not walk through original blocker2 with the authored bridge camera");
                const Vec2 arrival{world.actor.position[0],world.actor.position[2]};
                require(floorHeight(world.scene.floor,arrival).has_value()&&clearGround(world.scene.ground,arrival,0),"original BM02 arrival is outside the floor/inside a solid");
                require(world.transitions>0,"original portal0 did not record its transition");
                std::cout<<"Original opened bridge walk: "<<walked<<" input frames -> original portal0/BM02, camera"<<world.actor.camera<<'\n';return;
            }
            const Vec2 current{world.actor.position[0],world.actor.position[2]};
            require(floorHeight(world.scene.floor,current).has_value()&&clearGround(world.scene.ground,current,world.actor.clearanceRadius),"walking left floor or penetrated an enabled source solid");
            require(result.moved<=result.requested+ContactSkin*4,"walking resolver exceeded requested displacement");
            progress=dot(current-start,route)/distance;
            if(result.requested>.01f&&result.moved<result.requested*.1f)++blocked;
            if(!opened)require(!barrier.contains({current.x,0,current.z}),"closed blocker2 was penetrated");
        }
        require(!opened&&blocked>30&&progress>.1f&&progress<distance-.6f,"closed route did not stop at source blocker2");
        const Vec2 stopped{world.actor.position[0],world.actor.position[2]};
        require(std::any_of(world.scene.ground.begin(),world.scene.ground.end(),[&](const auto& polygon){return polygon.enabled&&polygon.sourceIndex==2&&
            nearestBoundary(polygon,stopped).distance<=world.actor.clearanceRadius+.002f;}),"closed walking stopped somewhere other than blocker2");
        std::cout<<"Original closed bridge walk: "<<walked<<" input frames, "<<blocked<<" blocked at source2, progress"<<progress<<"m, camera"<<world.actor.camera
            <<", clearanceRadius"<<world.actor.clearanceRadius<<", nativeRadiusCm"<<world.actors->at({0,0}).radius<<'\n';
    };
    const auto before=world.saveStateBytes();crossing(false);world.restoreStateBytes(before,nullptr);
    require(world.saveStateBytes()==before,"closed walking trial did not restore its full source state byte-exactly");
    continueLevelBStoryWorld(world,[&](World&){crossing(true);});
}
