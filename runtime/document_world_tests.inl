// Source pickup fixtures isolate reading; the story proof earns its own documents.
void testDocumentsWorld(const fs::path& levelA,bool draw,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize source document proof");
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Original documents: ")+message);};
    std::unique_ptr<Graphics> graphics;
    for(bool b:{false,true}) {
        const auto root=b?levelA/"levelB":levelA;World world(root,nullptr,presentMovieHeadless,true);
        auto& vm=world.game.vm;const auto I=[](int n){return jpc::Value::integer(n);};
        std::vector<std::pair<size_t,int>> candidates;
        for(int id:{17,72,73,74,75,76,130,131,136,152,153,159,160,165,166,169,171,172,173,175,210}) {
            const auto text=vm.invoke(world.game.package.methodIndex("ObjDB","getObjectTextInventoryDescription","(I)I"),0,{I(id)});
            try{candidates.push_back({world.sourceTexts().lookup(episode_portal_detail::sceneId(world.scene.id),uint32_t(text.integer())).size(),id});}catch(const jpc::Error&){}
        }
        std::sort(candidates.rbegin(),candidates.rend());
        const auto tailDocument=std::find_if(candidates.begin(),candidates.end(),[](const auto& candidate){return candidate.second==173;});
        require(tailDocument!=candidates.end(),"source trailing-whitespace document is unavailable");std::rotate(candidates.begin(),tailDocument,tailDocument+1);
        for(const auto& [size,id]:candidates){(void)size;if(world.game.items(100).size()==14)break;
            vm.invoke(world.game.package.methodIndex("Ash","pickupItem","(II)Z"),world.game.ash,{I(id),I(1)});}
        require(world.game.items(100).size()==14,"real source pickups did not populate the fourteen document slots");
        const auto book=[&](){statebytes::Writer out;for(const auto& item:world.game.items(100)){out.put(item.itemID);out.put(item.quantity);out.put(item.slot);}return out.bytes;};
        const auto initial=book();world.openInventory();world.syncInventory();unsigned read=0;int paged=-1;
        for(size_t n=0;n<world.inventoryView.items.size();++n)if(world.inventoryView.items[n].containerField==100) {
            world.inventoryView.selected=unsigned(n);world.applyInventoryItem();world.sampleSourceTexts();
            require(world.inventoryView.reading&&world.inventoryView.documentPage&&!world.inventoryView.documentPage->glyphs.empty(),"source document leaf did not produce a page");
            const auto next=world.inventoryView.documentPage->nextOffset;
            const auto text=world.documentTextID(world.inventoryView.items[n]);
            if(next&&!edguitext::layout(world.sourceTexts().lookup(episode_portal_detail::sceneId(world.scene.id),uint32_t(text.integer())),next,{44,84,596,414}).glyphs.empty())paged=int(n);
            require(book()==initial,"reading consumed or changed a document");++read;world.closeDocument();
        }
        require(read==14&&paged>=0,"physical slots12/13 or a genuine multipage document were not exercised");
        const auto tail=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==100&&item.itemID==173;});
        require(tail!=world.inventoryView.items.end(),"source tail document was not picked up");world.inventoryView.selected=unsigned(tail-world.inventoryView.items.begin());
        world.applyInventoryItem();world.sampleSourceTexts();
        for(unsigned n=0;n<40&&!world.inventoryView.documentPage->glyphs.empty();++n){world.applyInventoryItem();world.sampleSourceTexts();world.sampleSourceTexts();}
        require(world.inventoryView.documentPage->glyphs.empty(),"original trailing-whitespace page did not reach its empty tail");
        world.closeDocument();world.applyInventoryItem();
        require(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).textOffset==0&&!world.inventoryView.documentPage->glyphs.empty(),"reopening the same source document retained its empty tail");
        world.applyInventoryItem();require(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).flags&0x200u,"pending-page fixture was not queued");
        world.closeDocument();world.applyInventoryItem();
        require(!(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).flags&0x200u)&&SourceGui::at(world.sourceGui.items,world.documentReaderItem()).textOffset==0,
            "Back/reopen retained a pending page request");world.closeDocument();
        world.inventoryView.selected=unsigned(paged);world.applyInventoryItem();world.sampleSourceTexts();
        auto& record=SourceGui::at(world.sourceGui.items,world.documentReaderItem());
        const auto old=record.textOffset;world.applyInventoryItem();
        require((record.flags&0x200u)&&record.textOffset==old,"source next-page request was not deferred until rendering");
        world.sampleSourceTexts();require(record.textOffset!=old,"genuine document did not advance to its next page");
        world.sampleSourceTexts();const auto saved=world.saveStateBytes();
        world.closeDocument();world.closeInventory();world.restoreStateBytes(saved);
        require(world.inventoryView.open&&world.inventoryView.reading&&world.inventoryView.documentPage&&world.saveStateBytes()==saved,"v15 did not restore the active reader/cursor exactly");
        const auto before=world.saveStateBytes();record.textOffset=32767;const auto bad=world.saveStateBytes();world.restoreStateBytes(before);
        bool rejected=false;try{world.restoreStateBytes(bad);}catch(const std::exception&){rejected=true;}
        require(rejected&&world.saveStateBytes()==before,"invalid incoming document cursor changed the live World");
        locale_save_test::check(world,root);
        if(draw) {
            if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);}
            std::unique_ptr<Graphics::ProfileScope> profile;
            if(!graphics->sourceAssets.empty())profile=std::make_unique<Graphics::ProfileScope>(*graphics);
            graphics->loadFont(root);graphics->loadInventory(root);graphics->loadActorTextures(root,*world.actors);
            graphics->commit(graphics->prepare(world.scene,world.directory()));if(profile)profile->commit();
            auto frame=[&](){SDL_PumpEvents();return graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false,{},&world.inventoryView,
                world.actors.get(),world.drawObjects(),world.drawEffects(),world.drawActorTints(),world.lastOverlay,world.lastProjectionShake,world.sourceTextPages,world.drawGuiSprites());};
            for(unsigned n=0;n<3;++n)frame();const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
            if(mode&&width==mode->w&&height==mode->h){graphics->setFullscreen(true);}
            bool shown=false;for(unsigned n=0;n<30&&!shown;++n)shown=frame()&&(!mode||width!=mode->w||height!=mode->h||
                (graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));
            require(shown,"document GPU frame was unavailable");
            if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+(b?"-b.bmp":"-a.bmp")));
            world.closeDocument();world.applyInventoryItem();world.sampleSourceTexts();require(frame(),"first document page GPU frame was unavailable");
            if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+(b?"-b-first.bmp":"-a-first.bmp")));
        }
        std::cout<<"Original documents "<<(b?"B":"A")<<":14 real pickups/physical slots12/13, original description/reader leaf, paging, no consumption, v15 exact save and bad-cursor rollback passed\n";
    }
    graphics.reset();SDL_Quit();
}
