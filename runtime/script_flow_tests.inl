void testScriptFlow(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize script flow test");
    World world(root);
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Script flow: ")+message);};
    const auto originalObject=world.nativeProperties.objects.at(0);
    const int translate=world.game.package.methodIndex("ed/base/External","objTranslatePosition","(IIII)V");
    const auto I=[](int value){return jpc::Value::integer(value);};
    world.game.vm.invoke(translate,0,{I(0),I(123),I(-45),I(67)});
    const auto& movedObject=world.nativeProperties.objects.at(0);
    require(movedObject.position.x==originalObject.position.x+float(123*double(.01f))&&
        movedObject.position.y==originalObject.position.y+float(-45*double(.01f))&&
        movedObject.position.z==originalObject.position.z+float(67*double(.01f))&&movedObject.marker==-1&&
        movedObject.flags==originalObject.flags&&movedObject.cachedFloorHeight==originalObject.cachedFloorHeight,
        "native object translation changed position units or floor mode");
    world.nativeProperties.objects.at(0)=originalObject;
    world.game.vm.invoke(translate,0,{I(0),I(0),I(0),I(0)});
    require(world.nativeProperties.objects.at(0).marker==originalObject.marker,"zero native translation changed marker provenance");
    const Vec2 position{2.45914788f,8.35600605f};const auto marker=world.scene.scripting.markers[33];
    world.fixture(position,std::atan2(marker.x-position.x,marker.z-position.z));
    std::unique_ptr<Graphics> graphics;
    if(draw){graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
        graphics->loadFont(root);graphics->loadInventory(root);}
    SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.key=SDLK_E;
    world.step({},false,keyboardUseEvent(key),0,graphics.get());
    require(world.game.vm.staticField(418).integer()==1&&world.inventoryView.open&&
        world.inventoryView.items.size()==1&&world.inventoryView.items[0].itemID==119,"original pickup or inventory opening failed");
    auto render=[&](const std::string& suffix){
        if(!graphics)return;
        bool done=false;for(unsigned i=0;i<20&&!done;++i){SDL_Event event;while(SDL_PollEvent(&event))
            if(event.type==SDL_EVENT_QUIT)throw std::runtime_error("Script proof cancelled");
            done=graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,
                world.actor.animationTime,world.actor.walking,world.hud(),&world.inventoryView);}
        require(done,"inventory frame unavailable");
        if(!capture.empty())graphics->screenshot(suffix.empty()?capture:capture.parent_path()/(capture.stem().string()+suffix+capture.extension().string()));
    };
    render("");
    const auto collected=world.game.vm.snapshot();
    world.closeInventory();world.use();
    require(world.game.items().size()==1&&world.game.messageAsset==0x318a0804,"source empty-location branch duplicated the item");
    world.game.restore(collected);world.openInventory();
    world.game.vm.setField(world.game.ash,13,jpc::Value::integer(50));world.syncInventory();
    const auto id=attachVirtualGamepad();auto* pad=checked(SDL_OpenGamepad(id),"Open inventory test gamepad");
    bool held=false;
    checked(SDL_SetJoystickVirtualButton(SDL_GetGamepadJoystick(pad),SDL_GAMEPAD_BUTTON_SOUTH,true),"Press inventory South");
    SDL_UpdateGamepads();
    if(pressedEdge(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_SOUTH),held))world.applyInventoryItem();
    require(!pressedEdge(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_SOUTH),held),"held South repeated item use");
    require(world.game.vm.field(world.game.ash,13).integer()==85&&world.game.items().empty(),"source vitamin use did not heal35 and consume one item");
    render("-used");
    world.game.restore(collected);world.syncInventory();
    require(world.game.items().size()==1&&world.game.vm.staticField(418).integer()==1,"VM restore lost inventory/mission graph");
    SDL_CloseGamepad(pad);checked(SDL_DetachVirtualJoystick(id),"Detach inventory test gamepad");
    graphics.reset();SDL_Quit();
    std::cout<<"Script flow passed: actual AK01 pickup, original inventory menu assets, keyboard/SDL South, healing35, consumed slot, repeat and VM restoration\n";
}
