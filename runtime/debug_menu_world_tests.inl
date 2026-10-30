#pragma once
// Explicit item/debug combat fixtures; this is not an earned-story proof.
namespace debug_menu_test {
static void requireDebug(bool value,const char* message){if(!value)throw std::runtime_error(std::string("Debug menu: ")+message);}
static int quantity(World& w,int id){int total=0;for(int field:{98,99,100})for(const auto& item:w.game.items(field))if(item.itemID==id)total+=item.quantity;return total;}
static void readyDebug(World& w,unsigned count=90){for(unsigned n=0;n<count;++n){if(w.game.mode!=0)w.closeInventory();w.step({},false,false,.033f);requireDebug(!w.scriptFault,"source frame failed");}}
static void useDebugItem(World& w,int id,int container){w.openInventory();w.syncInventory();const auto found=std::find_if(w.inventoryView.items.begin(),w.inventoryView.items.end(),[&](const auto& item){return item.itemID==id&&item.containerField==container;});
    requireDebug(found!=w.inventoryView.items.end(),"UI item missing");w.inventoryView.selected=unsigned(found-w.inventoryView.items.begin());w.applyInventoryItem();w.closeInventory();readyDebug(w);}
static void pressDebug(World& w,ControlAction action,bool aim=true){ControlFrame input;input.down[size_t(ControlAction::Aim)]=aim;input.edges[size_t(action)]=true;input.down[size_t(action)]=true;w.playerControls(input);}
static void aimDebug(World& w,unsigned count){for(unsigned n=0;n<count;++n){ControlFrame f;f.down[size_t(ControlAction::Aim)]=true;w.playerControls(f);w.step({},false,false,.033f);requireDebug(!w.scriptFault,"aim source frame failed");}}
static void testDebugDamage(World& w,DebugMenu& menu){
    w.endEpisodeScene();Scene next=loadScene(w.root/"au04");Actor placed=resetActor(next,0);std::vector<GateState> gates(next.doors.size());w.enterSourceScene(next,placed,gates);
    w.scene=std::move(next);w.actor=std::move(placed);w.gates=std::move(gates);auto& vm=w.game.vm;
    for(unsigned n=0;n<180&&!w.actors->bodies().count({18,0});++n)w.step({},false,false,.033f);
    requireDebug(w.actors->bodies().count({18,0})!=0,"natural Annie was not created");
    const auto enemy=w.actors->at({18,0}).script;
    auto tick=[&](){const auto& npc=w.actors->at({18,0});auto& ash=w.actors->at({0,0});const Vec2 point{npc.pose.x+1.1f*std::sin(npc.pose.yaw),npc.pose.z+1.1f*std::cos(npc.pose.yaw)};
        requireDebug(floorHeight(w.scene.floor,point).has_value()&&clearGround(w.scene.ground,point,w.scene.radius+.3f),"natural enemy front has no clear source-floor fixture");
        w.positionProgressBody(ash,{point.x,0,point.z});w.publishProgressBody(ash);w.actor.clearanceRadius=initialActorRadius(w.scene,point);w.actor.movement.held=false;
        w.step({},false,false,.033f);requireDebug(!w.scriptFault,"natural incoming source damage frame failed");};
    int old=vm.field(w.game.ash,13).integer();for(unsigned n=0;n<1200&&vm.field(w.game.ash,13).integer()==old;++n)tick();
    const int hurt=vm.field(w.game.ash,13).integer();requireDebug(hurt<old&&hurt>0,"off state did not receive a real NPC attack");
    menu.settings.invulnerable=true;for(unsigned n=0;n<360;++n)tick();requireDebug(vm.field(w.game.ash,13).integer()==hurt,"god state took natural damage");
    const auto invulnerable=w.saveStateBytes();menu.settings.invulnerable=false;requireDebug(w.saveStateBytes()==invulnerable,"session god flag serialized");
    for(unsigned n=0;n<1200&&vm.field(w.game.ash,13).integer()==hurt;++n)tick();const int hurtAgain=vm.field(w.game.ash,13).integer();requireDebug(hurtAgain<hurt&&hurtAgain>0,"off state did not restore real damage");
    requireDebug(vm.field(enemy,13).integer()==100,"god test attacked or rewrote enemy health");
    std::cout<<"Natural source Annie/ActorDB/703 hits off "<<old<<"->"<<hurt<<", on360frames stable, off->"<<hurtAgain<<" passed (Ash-only positional combat fixture)\n";
}
}
void testDebugMenuWorld(const fs::path& root,const fs::path& capture={}) {
    using namespace debug_menu_test;SDL_SetMainReady();checked(SDL_Init(SDL_INIT_GAMEPAD|(!capture.empty()?SDL_INIT_VIDEO:0)),"Initialize debug menu proof");
    struct Quit {~Quit(){SDL_Quit();}} quit;std::cout<<std::unitbuf;
    World w(root,nullptr,presentMovieHeadless,true);DebugMenu menu;w.debugSettings=&menu.settings;
    requireDebug(!menu.settings.invulnerable&&!menu.settings.infiniteAmmo&&!menu.settings.infiniteFuel&&!menu.open,"session defaults enabled");
    readyDebug(w);const auto initial=w.saveStateBytes();menu.items=w.debugItemCatalog();
    for(int id:{3,10,40,41,55,73,100,101,102,170,203})requireDebug(std::any_of(menu.items.begin(),menu.items.end(),[&](const auto& item){return item.id==id;}),"valid weapon/document/quest item absent");
    requireDebug(std::none_of(menu.items.begin(),menu.items.end(),[](const auto& item){return item.id==0||item.id==1||item.id==2||item.id==16||item.id==38||item.id==140||item.id==65535;}),"invalid or source-template catalog entry");
    requireDebug(w.saveStateBytes()==initial,"catalog changed progress");
    for(unsigned n=0;n<menu.items.size();++n){menu.item=n;menu.notice=std::string(200,'x');std::istringstream lines(menu.text());std::string line;while(std::getline(lines,line))requireDebug(line.size()<=34,"debug text exceeds existing 640px font viewport");}menu.item=0;menu.notice.clear();
    for(const auto args:std::array<std::array<int,2>,5>{{{{65535,1}},{{-1,1}},{{3,0}},{{3,-1}},{{3,1000}}}})requireDebug(!w.debugGiveItem(args[0],args[1])&&w.saveStateBytes()==initial,"invalid grant mutated progress");
    const int ammo=quantity(w,3);requireDebug(w.debugGiveItem(3,3)&&quantity(w,3)==ammo+3,"source ammo receipt differs");
    requireDebug(w.debugGiveItem(41,1)&&quantity(w,41)==1,"source weapon receipt differs");
    requireDebug(w.debugGiveItem(73,1)&&quantity(w,73)==1,"source document receipt differs");
    requireDebug(w.debugGiveItem(55,1)&&quantity(w,55)==1,"source quest receipt differs");
    const auto receipts=w.saveStateBytes();bool fullRejected=false;
    for(int id:{29,30,34,36,44,47,48,53,54,56,82,88,93,99,107}){const auto before=w.saveStateBytes();if(!w.debugGiveItem(id,1)){
        requireDebug(w.saveStateBytes()==before,"source inventory rejection mutated state");menu.selected=0;menu.item=unsigned(std::find_if(menu.items.begin(),menu.items.end(),[&](const auto& item){return item.id==id;})-menu.items.begin());w.debugConfirm(menu);
        requireDebug(menu.selecting&&w.saveStateBytes()==before,"opening source item picker mutated World");w.debugConfirm(menu);
        requireDebug(menu.notice.find("not received")!=std::string::npos,"source grant failure was invisible");fullRejected=true;break;}}
    requireDebug(fullRejected,"source inventory capacity was not reached");w.restoreStateBytes(receipts,nullptr);
    std::cout<<"Source catalog "<<menu.items.size()<<" entries; real ammo, weapon, document, quest receipts and invalid atomic failures passed\n";
    w.restoreStateBytes(initial,nullptr);readyDebug(w);useDebugItem(w,40,98);useDebugItem(w,3,99);
    auto& vm=w.game.vm;auto gun=vm.field(w.game.ash,103).reference();requireDebug(vm.field(gun,812).integer()==40&&vm.field(gun,815).integer()>0,"source UI Gun/reload absent");
    auto shot=[&](){const int before=vm.field(gun,815).integer();pressDebug(w,ControlAction::SecondaryAttack);aimDebug(w,2);const int after=vm.field(gun,815).integer();aimDebug(w,30);return std::pair{before,after};};
    aimDebug(w,50);const auto normal=shot();requireDebug(normal.second==normal.first-1,"normal shot did not consume one");
    menu.settings.infiniteAmmo=true;w.debugResources();const int reserve=quantity(w,3);const auto endless=shot();requireDebug(endless.first==vm.field(gun,817).integer()&&endless.second==endless.first&&quantity(w,3)==reserve,"ammo cheat changed reserve or failed to refill");
    menu.settings.infiniteAmmo=false;const auto resumed=shot();requireDebug(resumed.second==resumed.first-1,"ammo off did not resume consumption");
    std::cout<<"Real Gun UI/control shots: off "<<normal.first<<"->"<<normal.second<<", on "<<endless.first<<"->"<<endless.second<<", off "<<resumed.first<<"->"<<resumed.second<<" passed\n";
    w.restoreStateBytes(initial,nullptr);readyDebug(w);auto saw=vm.field(w.game.ash,102).reference();
    auto powered=[&](){pressDebug(w,ControlAction::ChainsawRev);for(unsigned n=0;n<120&&vm.field(saw,139).integer()!=2;++n)aimDebug(w,1);requireDebug(vm.field(saw,139).integer()==2,"real motor did not idle");pressDebug(w,ControlAction::ChainsawRev);aimDebug(w,2);};
    powered();const int fuel=vm.field(saw,815).integer();pressDebug(w,ControlAction::PrimaryAttack);aimDebug(w,45);const int spent=vm.field(saw,815).integer();requireDebug(spent<fuel,"normal powered attack did not consume fuel");
    menu.settings.infiniteFuel=true;w.debugResources();const int full=vm.field(saw,817).integer();pressDebug(w,ControlAction::PrimaryAttack);aimDebug(w,45);requireDebug(vm.field(saw,815).integer()==full,"fuel cheat failed to refill");
    menu.settings.infiniteFuel=false;pressDebug(w,ControlAction::PrimaryAttack);aimDebug(w,45);requireDebug(vm.field(saw,815).integer()<full,"fuel off did not resume consumption");
    std::cout<<"Real chainsaw Rev/Primary: off fuel"<<fuel<<"->"<<spent<<", on "<<full<<", off "<<vm.field(saw,815).integer()<<" passed\n";
    w.restoreStateBytes(initial,nullptr);requireDebug(w.saveStateBytes()==initial,"debug settings leaked into save codec");
    SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.scancode=SDL_SCANCODE_PAUSE;requireDebug(DebugMenu::toggleKey(key),"Pause key did not select debug");key.key.repeat=true;requireDebug(!DebugMenu::toggleKey(key),"repeat toggled debug");key.key.repeat=false;key.key.scancode=SDL_SCANCODE_ESCAPE;requireDebug(!DebugMenu::toggleKey(key),"Escape changed ordinary pause binding");key.type=SDL_EVENT_KEY_UP;key.key.scancode=SDL_SCANCODE_PAUSE;requireDebug(!DebugMenu::toggleKey(key),"key-up toggled debug");
    menu.selected=1;menu.quantity=999;menu.move(1);requireDebug(menu.quantity==999,"quantity upper bound moved");menu.quantity=1;menu.move(-1);requireDebug(menu.quantity==1,"quantity lower bound moved");menu.selected=0;menu.move(-4);requireDebug(menu.selected==5,"up row wrap differs");menu.move(4);requireDebug(menu.selected==0,"down row wrap differs");
    menu.toggle();const auto paused=w.saveStateBytes();w.setOverlayPaused(true);const auto tick=w.sourceTick;w.advanceIdleTime(.1);requireDebug(w.sourceTick==tick,"debug overlay advanced actor simulation");menu.open=false;w.setOverlayPaused(false);
    w.restoreStateBytes(paused,nullptr);requireDebug(w.saveStateBytes()==paused,"overlay changed persistent flags");
    World fresh(root,nullptr,presentMovieHeadless,true);requireDebug(!fresh.debugSettings,"new session inherited debug settings");
    std::cout<<"Session flags absent from save/fresh World; overlay freezes simulation; all debug resource/item checks passed\n";
    readyDebug(w);const auto damageBase=w.saveStateBytes();const int damageHealth=vm.field(w.game.ash,13).integer();
    auto manualHit=[&](){w.actors->post({{0,0},{4,0},40,{{7,0,0,0}}});w.step({},false,false,.033f);requireDebug(!w.scriptFault,"manual source event40 failed");return vm.field(w.game.ash,13).integer();};
    menu.settings.invulnerable=true;requireDebug(manualHit()==damageHealth,"god event40 changed player health");w.restoreStateBytes(damageBase,nullptr);menu.settings.invulnerable=false;const int selfHit=manualHit();requireDebug(selfHit<damageHealth,"manual source event40 did not reach inherited CalcDamage");
    std::cout<<"Explicit source event40/data7 ->703 boundary: on HP"<<damageHealth<<", off HP"<<selfHit<<" passed\n";
    w.restoreStateBytes(initial,nullptr);testDebugDamage(w,menu);
    World b(fs::path(root)/"levelB",nullptr,presentMovieHeadless,true);DebugMenu bMenu;b.debugSettings=&bMenu.settings;readyDebug(b);bMenu.items=b.debugItemCatalog();const int bAmmo=quantity(b,3);
    requireDebug(b.debugGiveItem(3,2)&&quantity(b,3)==bAmmo+2,"B source ammo receipt differs");requireDebug(std::any_of(bMenu.items.begin(),bMenu.items.end(),[](const auto& item){return item.id==143;}),"B valid key missing");
    std::cout<<"LevelB source catalog "<<bMenu.items.size()<<" entries and real ammo receipt passed\n";
    if(!capture.empty()){World visual(root,nullptr,presentMovieHeadless,true);readyDebug(visual);DebugMenu overlay;overlay.items=visual.debugItemCatalog();overlay.toggle();overlay.settings.invulnerable=true;overlay.notice=std::to_string(overlay.items.size())+" valid source items; session only";const auto before=visual.saveStateBytes();
        {Graphics graphics;graphics.initialize(visual.scene,visual.directory(),1280,720);graphics.loadFont(visual.root);graphics.loadInventory(visual.root);graphics.loadActorTextures(visual.root,*visual.actors);
        for(unsigned n=0;n<3;++n){SDL_PumpEvents();graphics.render(visual.scene,visual.actor.camera,visual.actor.position,visual.actor.yaw,visual.actor.animationTime,false,overlay.text(),nullptr,visual.actors.get(),visual.drawObjects(),visual.drawEffects(),visual.drawActorTints(),visual.lastOverlay);}
        graphics.screenshot(capture);}requireDebug(visual.saveStateBytes()==before,"render-only menu mutated World");std::cout<<"Vulkan debug menu render zero World steps / exact bytes passed\n";}
}
