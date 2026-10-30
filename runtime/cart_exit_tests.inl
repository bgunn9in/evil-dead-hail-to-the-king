#pragma once
#include "cart_world_tests.inl"
namespace cart_world_test {
inline void inventorySaveExit(World& world,const fs::path& prefs) {
    require(world.game.vm.staticField(844).integer()==2&&active(world)=="MenSaveLoad"&&world.sourceMenuOwnsInput(),"inventory Save exit fixture is not source mode2");
    require(world.sourceMenuInput({111}),"inventory Save slot refused confirm");
    // An existing valid slot opens the original overwrite confirmation. Its
    // item1 branch consumes the tape and queues the actual selected Delete.
    const auto& system=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem);
    if(system.selected>=0&&SourceGui::at(world.sourceGui.items,system.selected).ordinal==0)world.sourceMenuInput({106});
    world.sourceMenuInput({111});
    for(unsigned n=0;n<50;++n) {
        if(world.sourceCart->pending()) {
            const auto operation=world.sourceCart->checkpoint().pending->operation;
            if(operation==edcart::Operation::Save)break;
            require(operation==edcart::Operation::Delete||operation==edcart::Operation::Access,"unexpected original inventory Save operation");
            require(!processSourceCart(world)&&!world.sourceCart->error(),"inventory overwrite preparation failed");
        }
        tick(world);
    }
    require(world.sourceCart->pending()&&world.sourceCart->checkpoint().pending->operation==edcart::Operation::Save&&!world.sourceCart->done(),"inventory source Save did not queue after real overwrite");
    const auto slot=world.sourceCart->checkpoint().pending->slot;require(slot==0,"inventory Save selected another PC slot");
    require(!processSourceCart(world)&&world.sourceCart->done()&&!world.sourceCart->error(),"inventory Save atomic write failed");
    const auto saved=savefile::read(prefs,"quicksave.eds");auto restored=stageSavedWorld(world,saved);
    require(restored->saveStateBytes()==saved,"pending mode2 Save did not restore byte exactly");
    auto leave=[&](World& target) {
        tick(target);require(target.sourceMenuText().find("SIPA C0XPAHEHA")!=std::string::npos,"pending mode2 UI did not finish actual SaveGameEnd");
        require(target.sourceMenuInput({102}),"source Save success Back was not accepted");
        for(unsigned n=0;n<12&&active(target)=="MenSaveLoad";++n)tick(target);
        require(active(target)=="InvMenuButtons"&&!target.sourceMenuOwnsInput()&&target.game.vm.staticField(844).integer()==2,"Save success did not return to original inventory parent");
        // The native GUI dispatch remains the source of this fixture's Back;
        // do not call setInterfaceMode(0) or write applied/menu fields.
        // InvBase.actionSys deliberately discards102; generic cancel112
        // reaches InvMenuButtons.sysCancel -> native gameStateChange(0).
        target.sourceGui.dispatchInput(target.game.vm,{112});
        require(target.game.mode==0,"original inventory Back did not request mode0 (requested"+std::to_string(target.game.mode)+")");tick(target);
        require(target.game.mode==0&&target.game.vm.staticField(844).integer()==0&&!target.inventoryView.open&&!target.sourceMenuOwnsInput(),
            "original inventory Back did not restore playable mode0 (requested"+std::to_string(target.game.mode)+", applied"+
            std::to_string(target.game.vm.staticField(844).integer())+", active"+active(target)+")");
        // Native Cart.begin may queue a final catalog refresh during the
        // source parent callbacks. The normal app services Access outside VM.
        if(target.sourceCart&&target.sourceCart->pending()) {
            require(target.sourceCart->checkpoint().pending->operation==edcart::Operation::Access,"UI exit queued an unexpected file mutation");
            require(!processSourceCart(target)&&!target.sourceCart->error(),"post-UI actual catalog refresh failed");
        }
        require(!target.preparingScene&&!target.pendingSceneSwitch&&!target.quitRequested&&(!target.sourceCart||!target.sourceCart->pending())&&
            std::none_of(target.nativeProperties.movies.begin(),target.nativeProperties.movies.end(),[](const auto& movie){return !movie.played;}),
            "playable UI exit has queued work: preparing"+std::to_string(target.preparingScene)+" sceneSwitch"+std::to_string(bool(target.pendingSceneSwitch))+
            " quit"+std::to_string(target.quitRequested)+" cart"+std::to_string(target.sourceCart&&target.sourceCart->pending())+
            " operation"+(target.sourceCart&&target.sourceCart->pending()?std::to_string(int(target.sourceCart->checkpoint().pending->operation)):"none"));
        const auto playable=target.saveStateBytes();require(target.saveGame(prefs),"post-UI F5 atomic file write failed");
        require(savefile::read(prefs,"quicksave.eds")==playable,"post-UI statefile differs from its playable snapshot");
        auto copy=stageSavedWorld(target,savefile::read(prefs,"quicksave.eds"));
        require(copy->saveStateBytes()==playable&&copy->game.mode==0&&!copy->inventoryView.open,"post-UI playable file snapshot failed exact restoration");
    };
    leave(world);leave(*restored);
    require(world.game.vm.snapshot()==restored->game.vm.snapshot(),"loaded pending inventory UI exited differently from live Save");
}
}
void testCartExit(const fs::path& levelA) {
    using namespace cart_world_test;std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize full source inventory Save exit proof");
    struct Quit{~Quit(){SDL_Quit();}} quit;Scratch scratch(levelA);
    for(bool b:{false,true}) {
        const auto root=b?levelA/"levelB":levelA,prefs=scratch.directory(b?"b-exit":"a-exit");
        scratch.track(prefs/"quicksave.eds");World world(root,nullptr,presentMovieHeadless,true);world.configureSourceCart(prefs);
        world.saveGame(prefs);menuModes(world);inventorySaveExit(world,prefs);
        std::cout<<"Original inventory Save exit "<<(b?"B":"A")<<": source overwrite/tape, atomic file, pending restore, success Back -> inventory -> playable and exact snapshot passed\n";
    }
}
