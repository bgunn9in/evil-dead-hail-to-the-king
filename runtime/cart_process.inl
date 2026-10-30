// Called after VM/GUI callbacks return. A loaded World is committed by the outer owner after process() returns.
std::unique_ptr<World> processSourceCart(World& current,Graphics* graphics=nullptr) {
    if(!current.sourceCart||!current.sourceCart->pending())return {};
    std::unique_ptr<World> replacement;
    std::unique_ptr<Graphics::ProfileScope> profile;
    edcart::Hooks hooks;
    const auto request=*current.sourceCart->checkpoint().pending;
    hooks.validate=[&](const auto& bytes){auto checked=stageSavedWorld(current,bytes);return edcart::Preview{checked->scene.id,checked->rawClockSeconds};};
    hooks.save=[&]{return current.saveStateBytes(request.operation==edcart::Operation::Save?int(request.slot):-1);};
    hooks.load=[&](const auto& bytes){
        replacement=stageLoadGame(current,bytes);
        if(graphics) {
            profile=std::make_unique<Graphics::ProfileScope>(*graphics);
            graphics->loadFont(replacement->root);graphics->loadInventory(replacement->root);graphics->loadActorTextures(replacement->root,*replacement->actors);
            auto prepared=graphics->prepare(replacement->scene,replacement->directory());
            graphics->prepareCameraFrame(*prepared,replacement->scene,replacement->actor.camera);
            replacement->audio.openDevice();replacement->audioBank.openDevice();if(replacement->streams)replacement->streams->openDevice();
            graphics->commit(std::move(prepared));profile->commit();
        }
    };
    const auto result=current.sourceCart->process(hooks);
    if(result&&!result->success) {
        current.notice="could not complete save operation";current.noticeDoor=-1;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Cart retained current world/file: %s",result->diagnostic.c_str());
        return {};
    }
    return replacement;
}
