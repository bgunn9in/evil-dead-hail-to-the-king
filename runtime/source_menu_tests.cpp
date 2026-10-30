#include "script_game.h"
#include "source_gui.h"
#include "ui_strings.h"
#include "episode_portals.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <optional>

namespace fs=std::filesystem;
struct MenuWorld {
    ScriptGame game;SourceGui sourceGui;fs::path root;bool originalRuntime=true,levelBPackage=false,authoredUiComplete=false;
    std::vector<std::array<int32_t,4>> switches;
    std::vector<std::array<int,2>> keyboardCalls;
    MenuWorld(const fs::path& script,const fs::path& assets,bool b):game(script,false),root(assets),levelBPackage(b) {
        const auto bind=[&](const std::string& name,jpc::Interpreter::Native native) {
            for(const auto& method:game.package.methods)if(method.owner==game.package.classIndex("ed/base/External")&&method.name==name) {
                game.vm.bind("ed/base/External",name,method.signature,std::move(native));return;
            }
            throw jpc::Error("Missing menu fixture native: "+name);
        };
        for(const char* name:SourceGui::Setters)bind(name,[&,name](auto& vm,auto,const auto& args){return sourceGui.native(vm,name,args);});
        for(const char* name:{"guiSysCreate","guiItemCreate","guiSysGetFlag","guiItemGetDataID","guiSysAtOffset","guiSysSet"})
            bind(name,[&,name](auto& vm,auto,const auto& args){return sourceGui.native(vm,name,args);});
        for(const char* name:{"platform","checkRumbleAbility","getMilliseconds"})bind(name,[](auto&,auto,const auto&){return jpc::Value::integer(0);});
        bind("sndMgrGetStereo",[&](auto&,auto,const auto&){return jpc::Value::integer(sourceGui.stereo);});
        bind("sndMgrGetMasterVolume",[&](auto&,auto,const auto&){return jpc::Value::integer(sourceGui.masterVolume);});
        bind("sndMgrSetStereo",[&](auto&,auto,const auto& args){sourceGui.stereo=args[0].integer();return jpc::Value{};});
        bind("sndMgrSetMasterVolume",[&](auto&,auto,const auto& args){sourceGui.masterVolume=args[0].integer();return jpc::Value{};});
        bind("sndMgrGetSNDID",[](auto&,auto,const auto&){return jpc::Value::integer(-1);});
        bind("sndMgrPlaySoundVol",[](auto&,auto,const auto& args){assert(args[0].integer()==-1);return jpc::Value::integer(-1);});
        bind("debugPrintStringInt",[](auto&,auto,const auto&){return jpc::Value{};});
        bind("controlCamera",[](auto&,auto,const auto&){return jpc::Value{};}); // Recorded GUI presentation boundary only.
        bind("audioStop",[](auto&,auto,const auto&){return jpc::Value{};}); // Original PC RET.
        bind("rumbleP1",[](auto&,auto,const auto&){return jpc::Value{};}); // PC40A7A0 RET.
        bind("guiKeyboardConfigAction",[&](auto&,auto,const auto& args){
            const int action=args[0].integer();keyboardCalls.push_back({action,args[1].integer()});
            if(action==1||action==2||action==7)return jpc::Value::integer(0); // PC1/2 no-op; no input inhibition in this fixture.
            throw jpc::Error("Unimplemented source keyboard capture operation in menu fixture");
        });
        bind("sceneSwitch",[&](auto&,auto,const auto& args){switches.push_back({args[0].integer(),args[1].integer(),args[2].integer(),args[3].integer()});return jpc::Value{};});
        game.vm.invoke(game.package.methodIndex("Main","initialize","()V"));
    }
    std::optional<std::string> sourceCartText(uint32_t)const{return {};}
    // This source-GUI fixture exports only the original English menu catalog.
    // It deliberately has no translated presentation profile or walking scene.
    struct EnglishTextFixture {
        bool localized()const{return false;}
        bool contains(uint32_t,uint32_t)const{return false;}
        const std::string& lookup(uint32_t,uint32_t)const{throw jpc::Error("This English menu fixture has no translated text");}
    };
    const EnglishTextFixture& sourceTexts()const{static const EnglishTextFixture text;return text;}
    struct MenuSceneFixture {std::string id;};
    MenuSceneFixture nativeScene()const{return {levelBPackage?"bb01":"aa01"};}
    #include "source_menu.inl"
};

int main(int argc,char** argv) {
    try {
        if(argc<3)throw std::runtime_error("Pass originalscript and menu assetsroot [B]");
        MenuWorld world(argv[1],argv[2],argc>3);auto& vm=world.game.vm;
        const auto I=[](int value){return jpc::Value::integer(value);};
        assert(!world.sourceMenuOwnsInput()&&world.sourceMenuText().empty());
        vm.invoke(world.game.package.methodIndex("Interface","gameStateChange","(II)V"),0,{I(0),I(1)});
        assert(world.sourceMenuOwnsInput()&&world.sourceMenuConfirmEvent()==111&&!world.sourceOptionsMenuActive());world.sourceGui.validateReferences(vm);
        const auto text=world.sourceMenuText();assert(!text.empty()&&text.size()<=512&&text.find("> ")!=std::string::npos);
        const int main=world.sourceGui.activeSystem;const int first=world.sourceGui.systems[size_t(main)].selected;
        world.sourceGui.systems[size_t(main)].flags&=~1u;assert(world.sourceMenuOwnsInput());world.sourceGui.systems[size_t(main)].flags|=1u;
        jpc::Ref start=0;
        for(const auto& system:world.sourceGui.systems)if(system.pad&&world.game.package.classes[size_t(vm.objectClass(system.pad))].name=="MenStart")start=system.pad;
        assert(start);vm.invoke(world.game.package.methodIndex("GuiSys","enable","()V"),start);
        vm.invoke(world.game.package.methodIndex("GuiSys","setVisible","(Z)V"),start,{I(1)});
        assert(world.sourceGui.activeSystem!=main&&!world.sourceMenuText().empty());
        world.sourceMenuInput({111});assert(world.sourceGui.activeSystem==main);
        assert(world.sourceMenuInput({106})&&world.sourceGui.systems[size_t(main)].selected!=first);
        assert(world.sourceMenuInput({106,111})); // Main ordinal2 -> actual MenOptions.enable.
        const auto& active=world.sourceGui.systems[size_t(world.sourceGui.activeSystem)];
        assert(world.game.package.classes[size_t(vm.objectClass(active.pad))].name=="MenOptionsLeft"&&world.sourceMenuConfirmEvent()==111&&world.sourceOptionsMenuActive());
        const auto options=world.sourceMenuText();assert(!options.empty()&&options.size()<=512);
        const int stereo=world.sourceGui.stereo;world.sourceMenuInput({111});assert(world.sourceGui.stereo!=stereo);
        world.sourceMenuInput({102});assert(world.sourceGui.activeSystem==main);
        world.sourceMenuInput({111});
        assert(!world.sourceKeyboardMenuActive());
        world.sourceMenuInput({106,106,106,111}); // Source OptionsLeft ordinal3 opens Keyboard.
        assert(world.sourceKeyboardMenuActive());
        assert((world.keyboardCalls==std::vector<std::array<int,2>>{{7,0},{1,0}}));
        // The modern controls popup owns its text; original FACE IDs are never
        // replaced with guessed labels. Closing invokes the real source path.
        world.sourceKeyboardMenuClose();assert(!world.sourceKeyboardMenuActive());
        assert(world.keyboardCalls.back()[0]==2);
        assert(world.game.package.classes[size_t(vm.objectClass(world.sourceGui.systems[size_t(world.sourceGui.activeSystem)].pad))].name=="MenMain");
        assert(!world.sourceMenuText().empty());
        assert(world.sourceGui.activeSystem==main);
        // The first selected main row was retained; move back to NewGame.
        world.sourceMenuInput({105,105,111});
        assert((world.switches==std::vector<std::array<int32_t,4>>{{0x616b0100,2,4,160}}));
        assert(world.game.mode==0); // Request only; applied Interface mode remains1 until source end-frame.
        assert(world.sourceMenuOwnsInput());
        vm.invoke(world.game.package.methodIndex("Interface","gameStateChange","(II)V"),0,{I(1),I(0)});
        assert(!world.sourceMenuOwnsInput());
        vm.statics[844]=I(2);assert(!world.sourceMenuOwnsInput());
        world.authoredUiComplete=true;assert(world.sourceMenuOwnsInput());
        world.authoredUiComplete=false;assert(!world.sourceMenuOwnsInput());
        std::cout<<"Source menu Main/Options focus, original labels, stereo, cancel, NewGame and pending/applied modes passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
