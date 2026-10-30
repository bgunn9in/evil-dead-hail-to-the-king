// Included after World and the SDL input helpers; exercises the actual scene loader.
static void doorTestRequire(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(std::string("Door self-test: ")+message);
}
static size_t doorTestIndex(const World& world,unsigned portal) {
    for (size_t i=0;i<world.scene.doors.size();++i)
        if (world.scene.doors[i].portalIndex==portal) return i;
    throw std::runtime_error("Door self-test: source portal is missing");
}
struct DoorTestCopy {
    fs::path parent,path;
    explicit DoorTestCopy(const fs::path& source) {
        parent=fs::weakly_canonical(fs::absolute(source)).parent_path();
        path=parent/("door-self-test-assets-"+std::to_string(SDL_GetTicksNS()));
        if (path.parent_path()!=parent||!fs::create_directory(path)||fs::weakly_canonical(path)!=path)
            throw std::runtime_error("Cannot create isolated door-test assets");
        try { fs::copy(source/"ak09",path/"ak09",fs::copy_options::recursive); }
        catch (...) { cleanup(); throw; }
    }
    void cleanup() noexcept {
        std::error_code error;
        const auto resolved=fs::weakly_canonical(path,error);
        if (!error&&resolved==path&&resolved.parent_path()==parent&&
            resolved.filename().string().rfind("door-self-test-assets-",0)==0)
            fs::remove_all(resolved,error);
    }
    ~DoorTestCopy() { cleanup(); }
};

void testDoors(const fs::path& root,bool draw,int width,int height,const fs::path& capture,std::ostream* trace) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize door test");
    struct InputSession {
        SDL_JoystickID id=0;
        SDL_Gamepad* pad=nullptr;
        ~InputSession() { if (pad) SDL_CloseGamepad(pad); if (id) SDL_DetachVirtualJoystick(id); SDL_Quit(); }
    } session;
    session.id=attachVirtualGamepad();
    session.pad=checked(SDL_OpenGamepad(session.id),"Open door-test gamepad");
    auto* joystick=SDL_GetGamepadJoystick(session.pad);
    World world(root,trace);
    doorTestRequire(world.scene.id=="ak01","initial area is not AK01");
    std::unique_ptr<Graphics> graphics;
    bool exactDisplayProof=false;
    if (draw) {
        graphics=std::make_unique<Graphics>();
        graphics->initialize(world.scene,world.directory(),width,height);
        graphics->loadFont(root);
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if (mode&&width==mode->w&&height==mode->h) {
            // Drain creation/claim resize events before requesting a fullscreen change.
            for (unsigned frame=0;frame<3;++frame) {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                    if (event.type==SDL_EVENT_QUIT) throw std::runtime_error("Door self-test cancelled");
                graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,
                                 world.actor.animationTime,world.actor.walking,world.hud());
                SDL_Delay(1);
            }
            graphics->setFullscreen(true);
            exactDisplayProof=true;
        }
    }
    constexpr float dt=1.0f/30;
    bool heldSouth=false;
    bool displaySettled=false;
    unsigned captures=0;
    auto south=[&](bool down) {
        checked(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,down),"Set door-test South");
        SDL_UpdateGamepads();
        return pressedEdge(SDL_GetGamepadButton(session.pad,SDL_GAMEPAD_BUTTON_SOUTH),heldSouth);
    };
    auto keyboard=[&](SDL_Keycode key,bool repeat=false,Uint32 type=SDL_EVENT_KEY_DOWN) {
        SDL_Event event{}; event.type=type; event.key.key=key; event.key.repeat=repeat;
        return keyboardUseEvent(event);
    };
    doorTestRequire(keyboard(SDLK_E)&&keyboard(SDLK_RETURN)&&!keyboard(SDLK_E,true)&&
                    !keyboard(SDLK_E,false,SDL_EVENT_KEY_UP)&&!keyboard(SDLK_SPACE),"keyboard Use edge mapping");
    auto allClosed=[&]() {
        for(size_t i=0;i<world.gates.size();++i)if(!world.scene.doors[i].interaction.points.empty()&&
            (world.gates[i].enabled||world.gates[i].pending))return false;
        return true;
    };
    auto render=[&](bool save) {
        if (!graphics) return;
        bool rendered=false;
        for (unsigned frame=0;frame<30&&!rendered;++frame) {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                if (event.type==SDL_EVENT_QUIT) throw std::runtime_error("Door self-test cancelled");
            const bool frameRendered=graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,
                                                     world.actor.animationTime,world.actor.walking,world.hud());
            const bool matches=!exactDisplayProof||
                (graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height));
            if (frameRendered&&displaySettled)
                doorTestRequire(matches,"fullscreen proof dimensions changed after settling");
            rendered=frameRendered&&matches;
            if (!rendered) SDL_Delay(1);
        }
        doorTestRequire(rendered,"destination frame or requested fullscreen size did not settle within 30 frames");
        displaySettled=exactDisplayProof;
        if (save&&!capture.empty()) {
            ++captures;
            const auto file=captures==1?capture:capture.parent_path()/
                (capture.stem().string()+"-"+std::to_string(captures)+"-"+world.scene.id+capture.extension().string());
            if (!file.parent_path().empty()) fs::create_directories(file.parent_path());
            graphics->screenshot(file);
        }
    };
    auto turnTo=[&](size_t index) {
        const auto& door=world.scene.doors[index];
        const Vec2 difference{door.marker.x-world.actor.position[0],door.marker.z-world.actor.position[2]};
        const auto& camera=world.scene.cameras[world.actor.camera];
        const float determinant=camera.right.x*camera.forward.z-camera.right.z*camera.forward.x;
        doorTestRequire(std::abs(determinant)>0.01f,"singular arrival camera ground basis");
        Vec2 stick{(difference.x*camera.forward.z-difference.z*camera.forward.x)/determinant,
                   (camera.right.x*difference.z-camera.right.z*difference.x)/determinant};
        stick=stick*(1/length(stick));
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,Sint16(std::clamp(stick.x,-1.0f,1.0f)*32767)),"Set arrival turn X");
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,Sint16(std::clamp(-stick.z,-1.0f,1.0f)*32767)),"Set arrival turn Y");
        SDL_UpdateGamepads();
        const Vec2 before{world.actor.position[0],world.actor.position[2]};
        const float beforeY=world.actor.position[1];
        world.step(gamepadStick(session.pad),false,false,1.0f/60,graphics.get());
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0),"Release arrival turn X");
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,0),"Release arrival turn Y");
        SDL_UpdateGamepads();
        const Vec2 after{world.actor.position[0],world.actor.position[2]};
        const float moved=length(after-before);
        const auto height=floorHeight(world.scene.floor,after);
        doorTestRequire(moved>0.001f&&moved<0.05f&&height&&
                        std::abs(world.actor.position[1]-*height-world.actor.floorOffset)<1e-5f&&
                        clearGround(world.scene.ground,after,world.scene.radius)&&facingDoor(door,after,world.actor.yaw),
                        "virtual stick did not walk and face the return door on its source floor");
        if (world.scene.id=="ak09")
            doorTestRequire(std::abs(world.actor.position[1]-beforeY)>1e-6f&&insideTrigger(door.interaction,after)&&
                            insideDoorRegion(door.portal,world.scene.triggerGrid,after),
                            "exterior slope step lost floor height or the return gate");
        world.step({},false,false,0,graphics.get());
    };
    auto settleArrival=[&](unsigned returnPortal,bool keyboardHeld) {
        const auto transitions=world.transitions,attempts=world.attempts;
        const size_t index=doorTestIndex(world,returnPortal);
        turnTo(index);
        if (world.guard) {
            const unsigned guard=world.guard;
            world.step({},false,keyboard(SDLK_RETURN),0,graphics.get());
            doorTestRequire(world.guard==guard&&allClosed(),"45-tick guard accepted another Use");
        }
        for (unsigned tick=0;tick<45;++tick)
            world.step({},false,keyboardHeld?keyboard(SDLK_E,true):south(true),dt,graphics.get());
        doorTestRequire(!world.guard&&allClosed()&&world.transitions==transitions&&world.attempts==attempts,
                        "held Use repeated or arrival automatically bounced");
        south(false);
    };
    auto arrive=[&](unsigned portal,unsigned expectedTicks,double voiceSeconds,bool useKeyboard) {
        const size_t index=doorTestIndex(world,portal);
        const Door expected=world.scene.doors[index];
        const auto transitions=world.transitions,attempts=world.attempts;
        const auto tick=world.sourceTick;
        world.audio.duration=voiceSeconds;
        const bool edge=useKeyboard?keyboard(SDLK_E):south(true);
        doorTestRequire(edge,"Use did not produce a fresh edge");
        world.step({},false,edge,0,graphics.get());
        doorTestRequire(world.gates[index].pending&&!world.gates[index].enabled&&world.guard==45,
                        "Use did not queue the original disabled portal");
        if (useKeyboard&&graphics&&!capture.empty()) {
            render(false);
            const auto opening=capture.parent_path()/(capture.stem().string()+"-opening"+capture.extension().string());
            if (!opening.parent_path().empty()) fs::create_directories(opening.parent_path());
            graphics->screenshot(opening);
        }
        for (unsigned elapsed=1;elapsed<=expectedTicks;++elapsed) {
            world.step({},false,useKeyboard?keyboard(SDLK_E,true):south(true),dt,graphics.get());
            if (elapsed<expectedTicks)
                doorTestRequire(world.transitions==transitions&&world.attempts==attempts,"portal loaded before its source sound delay");
        }
        doorTestRequire(world.sourceTick-tick==expectedTicks&&world.transitions==transitions+1&&world.attempts==attempts+1,
                        "portal missed its 30/45-tick activation boundary");
        const auto height=floorHeight(world.scene.floor,{expected.spawn.x,expected.spawn.z});
        doorTestRequire(world.scene.id==expected.destination&&height&&
                        std::abs(world.actor.position[0]-expected.spawn.x)<1e-6f&&
                        std::abs(world.actor.position[2]-expected.spawn.z)<1e-6f&&
                        std::abs(world.actor.position[1]-*height-expected.spawn.y)<1e-5f&&
                        std::abs(world.actor.yaw-expected.yaw)<1e-6f&&world.actor.camera==expected.destinationCamera,
                        "arrival did not preserve source marker, local height, facing or activation camera");
        doorTestRequire(allClosed(),"destination activate did not disable both return portals");
        render(true);
    };
    auto fixture=[&](unsigned portal,Vec2 position,float yaw) {
        doorTestRequire(!world.guard&&allClosed(),"source-ready fixture would overwrite live state");
        const auto index=doorTestIndex(world,portal);
        world.fixture(position,yaw+3.14159265f,int(index));
        world.step({},false,keyboard(SDLK_E),0,graphics.get());
        doorTestRequire(allClosed()&&!world.guard,"look-away Use enabled a source portal");
        world.fixture(position,yaw,int(index));
        const auto attempts=world.attempts;
        doorTestRequire(!world.transfer(index,graphics.get())&&world.attempts==attempts,"closed portal bypassed Use");
        south(false);
    };

    fixture(3,{0.0643162f,11.2097956f},0.05967f);
    arrive(3,30,0,true);
    settleArrival(8,true);
    arrive(8,45,10,false);
    settleArrival(3,false);
    fixture(2,{0.0793162f,-1.9016995f},-3.1246268f);
    arrive(2,30,0,false);
    settleArrival(7,false);
    arrive(7,45,10,false);
    settleArrival(2,false);
    doorTestRequire(world.transitions==4&&world.scene.id=="ak01","both original door pairs were not reversible");

    // Fault injection touches only a verified temporary copy, never canonical assets.
    DoorTestCopy copy(root);
    auto missing=copy.path/"ak09"/(draw?"background_0.bmp":"floor.bin");
    if(draw)missing=edimage::colorPath(missing);
    doorTestRequire(fs::remove(missing),"could not remove isolated destination test asset");
    struct RootOverride {
        World& world; fs::path previous;
        RootOverride(World& value,const fs::path& path):world(value),previous(value.root) { world.root=path; }
        ~RootOverride() { world.root=std::move(previous); }
    } overrideRoot(world,copy.path);
    fixture(3,{0.0643162f,11.2097956f},0.05967f);
    const unsigned attempts=world.attempts,transitions=world.transitions;
    const auto activeMesh=world.scene.vertices.data();
    const auto activeGPU=graphics?graphics->resources.get():nullptr;
    const Actor before=world.actor;
    world.audio.duration=0;
    world.step({},false,south(true),0,graphics.get());
    for (unsigned tick=0;tick<30;++tick) world.step({},false,south(true),dt,graphics.get());
    doorTestRequire(world.attempts==attempts+1&&world.transitions==transitions&&world.scene.id=="ak01"&&
                    world.scene.vertices.data()==activeMesh&&(!graphics||graphics->resources.get()==activeGPU)&&
                    std::abs(world.actor.position[0]-before.position[0])<1e-6f&&
                    std::abs(world.actor.position[1]-before.position[1])<1e-6f&&
                    std::abs(world.actor.position[2]-before.position[2])<1e-6f&&world.actor.camera==before.camera,
                    "failed destination load changed the active scene, actor or GPU resources");
    for (unsigned tick=0;tick<45;++tick) world.step({},false,south(true),dt,graphics.get());
    doorTestRequire(world.attempts==attempts+1&&world.transitions==transitions,"failed held portal retried every frame");
    render(false);
    std::cout<<"Door self-test passed: 4 original transitions, keyboard/South edges, facing, 30/45-tick timing, "
             <<"45-tick Use guard, no bounce, and isolated "<<(draw?"GPU background":"CPU floor")<<" rollback\n";
}
