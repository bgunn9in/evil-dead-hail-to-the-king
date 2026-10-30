// Actual untouched JDC execution; the supplied profile determines scene/media provenance.
void testDreamcastRuntime(const fs::path& assets) {
 std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD),"DC probe SDL");
 struct Quit {~Quit(){SDL_Quit();}} quit;cart_world_test::Scratch scratch(assets);
 auto need=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
 auto active=[](World& w){auto& sys=SourceGui::at(w.sourceGui.systems,w.sourceGui.activeSystem);return w.game.package.classes.at(size_t(w.game.vm.objectClass(sys.pad))).name;};
 auto tick=[&](World& w){w.step({},false,false,1.0f/30);need(!w.scriptFault,w.game.diagnostic);need(!processSourceCart(w),"unexpected Cart replacement");};
  auto authoredInventory=[&](World& w) {
   if(!w.sourceGui.uvLayout)return;
   tick(w); // Cold restore intentionally rebuilds derived draw commands on the next source frame.
   need(w.authoredUiComplete&&w.sourceMenuOwnsInput(),"authored inventory did not own modern input");
   auto selection=[&](){return std::pair<int,int>{w.sourceGui.activeSystem,SourceGui::at(w.sourceGui.systems,w.sourceGui.activeSystem).selected};};
   const auto before=selection();bool navigated=false;
   for(int event:{103,104,105,106}){need(w.sourceMenuInput({event}),"authored inventory rejected navigation");tick(w);navigated|=selection()!=before;}
   need(navigated,"authored inventory navigation never changed source selection");
   const auto selected=w.saveStateBytes();
   need(w.sourceMenuInput({99}),"authored inventory rejected native use");tick(w);
   std::cout<<"AUTHORED_INVENTORY_USE "<<w.scene.id<<" owner="<<(w.sourceGui.activeSystem>=0?active(w):"none")<<" mode="<<w.game.mode<<'\n';
   w.restoreStateBytes(selected);tick(w);
   for(unsigned n=0;n<6&&w.game.mode==2;++n){need(w.sourceMenuInput({102}),"authored inventory rejected Back");tick(w);}
   need(w.game.mode==0,"authored inventory Back did not return to gameplay");
   std::cout<<"AUTHORED_INVENTORY_NAV_USE_BACK_PASS "<<w.scene.id<<'\n';
  };
 auto saveProof=[&](World& w,const char* tag){std::cout<<"PRE_SAVE "<<tag<<" scene="<<w.scene.id<<" preparing="<<w.preparingScene<<" switch="<<bool(w.pendingSceneSwitch)<<" quit="<<w.quitRequested<<" cart="<<bool(w.sourceCart&&w.sourceCart->pending())<<" movies=";for(auto& m:w.nativeProperties.movies)std::cout<<std::hex<<m.resource<<":"<<m.played<<",";std::cout<<std::dec<<"\n";const auto bytes=w.saveStateBytes();tick(w);w.restoreStateBytes(bytes);need(w.saveStateBytes()==bytes,std::string(tag)+" direct mismatch");auto cold=stageSavedWorld(w,bytes);need(cold->saveStateBytes()==bytes,std::string(tag)+" cold mismatch");std::cout<<"SAVE "<<tag<<" bytes="<<bytes.size()<<" direct+cold exact\n";};
 for(bool b:{false,true}) {
  std::cout<<"BOOT "<<(b?'B':'A')<<'\n';
  World w(b?assets/"levelB":assets,nullptr,presentMovieHeadless,true,nullptr,true);
  w.configureSourceCart(scratch.directory(b?"title-b":"title-a"));
  need(w.sourceCartNative("cartCountValidSlots",{}).integer()==8&&w.sourceCartNative("cartCountValidCardSlots",{}).integer()==1,"native Cart must expose eight files in one directory");
  need(w.sourceCartText(0xffff0100u)==edui::text("save files",true)&&w.sourceCartText(0xffff0101u)==std::string{}&&w.sourceCartText(0xffff0200u)==edui::text("empty",true),"native Cart directory/empty labels differ");
  need(w.sourceCartNative("cartDeleteGame",{jpc::Value::integer(0)}).integer()==1&&!w.sourceCart->pending(),"native overwrite preparation must retain the existing file");
  need(w.sourceCartAccess()==1&&w.sourceCartNative("cartDeleteGame",{jpc::Value::integer(0)}).integer()==0&&w.sourceCart->checkpoint().pending->operation==edcart::Operation::Access,"native overwrite preparation must reject a pending operation");
  w.sourceCartNative("cartEnd",{});
  for(unsigned n=0;n<1000&&!w.sourceMenuOwnsInput();++n)tick(w);
  need(w.game.profile.dreamcast&&active(w)=="MenStart","actual DC title did not own Start");
  for(unsigned n=0;n<90;++n)tick(w);
  const auto titleMusic=w.streams->snapshot();need(titleMusic.playing&&titleMusic.loop&&titleMusic.elapsed>.5,"title music was not already playing");
  need(w.sourceMenuInput({112}),"Start rejected confirm");need(active(w)=="MenMain","Start did not open Main");
  need(w.streams->snapshot().id==titleMusic.id&&w.streams->snapshot().elapsed==titleMusic.elapsed,"START restarted title music");
  std::cout<<"MENU "<<(b?'B':'A')<<" scene="<<w.scene.id<<" owner="<<active(w)<<"\n"<<w.sourceMenuText()<<"\n";
  unsigned rows=0;
  for(auto& item:w.sourceGui.items)if(item.owner==w.sourceGui.activeSystem&&(item.flags&9u)==9u)++rows;
  for(unsigned n=0;n<20;++n)tick(w);
  need(rows==3,"DC Main must contain 3 actual rows, got "+std::to_string(rows));
  saveProof(w,b?"B-title":"A-title");tick(w);
  need(w.authoredUiComplete&&w.sourceMenuOwnsInput()&&!w.notice.empty()&&w.hud().empty(),"restored host status must not cover the original menu");
  w.sourceMenuInput({106});w.sourceMenuInput({106});const auto mainMusic=w.streams->snapshot();
  w.enqueueNativeInput(99,1);for(unsigned n=0;n<8&&active(w)=="MenMain";++n)tick(w);
  std::cout<<"OPTIONS owner="<<active(w)<<"\n"<<w.sourceMenuText();need(active(w)=="MenOptionsLeft","Options not opened: "+active(w));
  need(w.streams->snapshot().playing&&w.streams->snapshot().id==mainMusic.id&&w.streams->snapshot().elapsed>=mainMusic.elapsed,"entering Options interrupted menu music");
  const auto optionsStarted=w.streams->snapshot();for(unsigned n=0;n<3;++n)tick(w);
  need(w.streams->snapshot().elapsed>optionsStarted.elapsed,"Options paused menu music");
  w.sourceMenuInput({106});const auto stereo=w.sourceGui.stereo;w.sourceMenuInput({111});need(w.sourceGui.stereo!=stereo,"original Options stereo did not toggle");tick(w);
  need(w.authoredUiComplete&&w.sourceMenuOwnsInput()&&w.hud().empty(),"original Options must own its presentation without prototype HUD hints");
  bool backFooter=false;const auto scope=episode_portal_detail::sceneId(w.nativeScene().id);
  for(const auto& command:w.sourceGuiCommands){
   need(command.item>=0&&size_t(command.item)<w.sourceGui.items.size(),"original Options contains a host-authored draw command");
   if(const auto* page=std::get_if<edguitext::Page>(&command.value)){
    const auto& item=w.sourceGui.items[size_t(command.item)];const auto data=item.properties.find("guiItemSetDataID");
    if(data!=item.properties.end()&&data->second.size()==1&&w.sourceTexts().contains(scope,uint32_t(data->second[0]))&&
       w.sourceTexts().originalText(scope,uint32_t(data->second[0])).find("-back")!=std::string::npos)
        backFooter|=!page->glyphs.empty();
   }
  }
  need(backFooter,"original Options did not render its source Back footer");
  saveProof(w,b?"B-options":"A-options");const auto optionsMusic=w.streams->snapshot();
  need(optionsMusic.playing&&optionsMusic.id==titleMusic.id,"Options lost the playing title music");
  w.sourceMenuInput({102});need(active(w)=="MenMain","Options did not close");
  need(w.streams->snapshot().playing&&w.streams->snapshot().id==optionsMusic.id&&w.streams->snapshot().elapsed==optionsMusic.elapsed,"return from Options restarted menu music");
  for(unsigned n=0;n<5;++n){auto& sys=SourceGui::at(w.sourceGui.systems,w.sourceGui.activeSystem);if(SourceGui::at(w.sourceGui.items,sys.selected).ordinal==0)break;w.sourceMenuInput({105});}
  need(w.sourceMenuInput({111})&&w.pendingSceneSwitch.has_value(),"New Game did not request scene");
  need(!w.streams->status(),"New Game retained title music");tick(w);
  const auto request=*w.pendingSceneSwitch;w.pendingSceneSwitch.reset();need(request.destinationId==0x616b0100u,"New Game target is not AK01");
  if(b){auto next=stagePackageWorld(w,request);need(next->scene.id=="ak01"&&next->game.profile.dreamcast&&!next->levelBPackage,"B NewGame cross-package is not DC A AK01");saveProof(*next,"B-newgame-AK01");}
   else {need(w.switchScene(request,nullptr)&&w.scene.id=="ak01","A New Game did not load AK01");saveProof(w,"A-newgame-AK01");continueEpisodeWalkWorld(w,false,640,480,{},[&](World& result){need(result.scene.id=="ah02","actual first8 did not reach AH02");saveProof(result,"A-first8-AH02");if(result.sourceGui.uvLayout){result.openInventory();tick(result);authoredInventory(result);}});}
  std::cout<<"TITLE_NEWGAME_PASS "<<(b?'B':'A')<<"\n";
 }
 std::cout<<"B_FRESH_BOOT\n";World b(assets/"levelB",nullptr,presentMovieHeadless,true);b.configureSourceCart(scratch.directory("world-b"));need(b.scene.id=="bm01","B original bootstrap mismatch");saveProof(b,"B-bm01");
 std::cout<<"B_START dc="<<b.game.profile.dreamcast<<" pos="<<b.actor.position[0]<<","<<b.actor.position[2]<<" speed="<<b.scene.speed<<" camera="<<b.actor.camera<<"\n";const auto movementStart=b.saveStateBytes();bool moved=false;ControlFrame input;
 for(Vec2 direction:std::array<Vec2,8>{{{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}}}) {
  b.restoreStateBytes(movementStart);const std::array<float,3> before{{b.actor.position[0],b.actor.position[1],b.actor.position[2]}};input.movement=direction;
  for(unsigned n=0;n<30;++n){b.playerControls(input);b.step(input.movement,n>=15,false,1.0f/30);need(!b.scriptFault,b.game.diagnostic);}
  const auto delta=length(Vec2{b.actor.position[0]-before[0],b.actor.position[2]-before[2]});
  std::cout<<"B_MOVE_RUN direction="<<direction.x<<","<<direction.z<<" state="<<b.actors->at({0,0}).actions.layers[0].state<<" delta="<<delta<<"\n";
  if(delta>.01f){moved=true;break;}
 }
 need(moved,"B modern movement did not move Ash in any direction");
 saveProof(b,"B-moving");b.playerControls(ControlFrame{});for(unsigned n=0;n<5;++n)tick(b);
  b.openInventory();tick(b);need(b.game.mode==2&&!b.inventoryView.items.empty(),"B source inventory did not open or is empty");saveProof(b,"B-inventory");authoredInventory(b);b.closeInventory();tick(b);need(b.game.mode==0,"B inventory did not close");
 b.setInterfaceMode(3);need(b.sourceMenuOwnsInput()&&active(b)=="Pause"&&b.sourceMenuConfirmEvent()==99,"B original Pause does not own native use input");std::cout<<"B_PAUSE "<<b.sourceMenuText()<<"\n";saveProof(b,"B-pause");need(b.sourceMenuInput({102})&&b.game.mode==0,"B modern Back did not resume original Pause");tick(b);need(!b.sourceMenuOwnsInput(),"B Pause retained input after resume");
 std::cout<<"B_CONTROLS_PASS\n";
 // An isolated source-camera fixture covers all four BL06 range endpoints and cold restoration.
 b.endEpisodeScene();Scene library=loadScene(assets/"levelB"/"bl06");Actor placed=resetActor(library,5);
 std::vector<GateState> libraryGates(library.doors.size());b.enterSourceScene(library,placed,libraryGates);
 b.scene=std::move(library);b.actor=std::move(placed);b.gates=std::move(libraryGates);
 for(int segment:{0,1})for(int selector:{1,2}) {
  const unsigned start=unsigned(segment*15+(selector==2?14:0));
  b.cameraTimeline->control({4,5,segment,selector,2});b.applyCameraFrame(b.scene);b.actor.camera=5;
  need(b.scene.foregroundFrame.at(5)==start,"BL06 foreground did not select the authored range start");
  for(unsigned n=0;n<3;++n)if(b.cameraTimeline->tick())b.applyCameraFrame(b.scene);
  need(b.scene.foregroundFrame.at(5)==start,"animated BL06 background changed its foreground range");
  const auto saved=b.saveStateBytes();b.scene.foregroundFrame.at(5)=999;b.restoreStateBytes(saved);
  need(b.scene.foregroundFrame.at(5)==start&&b.saveStateBytes()==saved,"BL06 foreground range was not rebuilt from the saved controller");
  const auto cold=stageSavedWorld(b,saved);
  need(cold->scene.foregroundFrame.at(5)==start&&cold->saveStateBytes()==saved,"cold BL06 restore lost its foreground range");
 }
 std::cout<<"BL06_FOREGROUND_RANGE_RESTORE_PASS\n";
}
