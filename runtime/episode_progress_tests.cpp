#include "script_game.h"
#include "source_gui.h"
#include "source_scene_queries.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <iostream>

static ScriptVolume rectangle(unsigned kind,float x0,float z0,float x1,float z1) {
    return {kind,0,{{x0,0,z0},{x1,0,z1}}};
}
int main(int argc,char** argv) {
    try {
        SourceSceneQueries queries;queries.grid={0,0,16,16};
        queries.volumes.push_back({rectangle(3,0,0,16,16),0xffffffff,2,3});
        queries.volumes.push_back({rectangle(4,8,8,10,10),0xffffffff,2,3});
        assert(queries.visible(2,3,{2,7,2}));assert(!queries.visible(4,3,{2,7,2}));
        assert(!queries.visible(2,2,{2,0,2}));assert(!queries.visible(2,3,{9,0,9}));
        assert(queries.extentMask(16,16,16,16)==0x00010001u);
        assert(queries.extentMask(16,16,16,16,true)==0x00010000u);
        assert(queries.extentMask(-100,-100,-100,-100)==0x00010001u);
        assert(queries.nativeExtentMask(16,16,16,16)==0x00010001u);
        assert(queries.nativeExtentMask(16,16,16,16,true)==0x00010000u);
        assert(queries.nativeExtentMask(0,0,16,16)==0xffffffffu);
        assert(queries.nativeExtentMask(15.9f,15.9f,16.1f,16.1f,true)==0x80018000u);
        assert(queries.nativeExtentMask(-2,-2,.1f,.1f)==0x00010001u);
        assert(queries.nativeExtentMask(-.1f,-.1f,.1f,.1f,true)==0x00010001u);
        queries.grid={3.1415901184f,-.8172300458f,7.123450279f,11.21921062f};
        assert(queries.nativeExtentMask(4.9224529266f,.5851712823f,5.3676686287f,1.2875418663f)==0x000c0030u);
        assert(queries.nativeExtentMask(4.822453f,.4851713f,5.022453f,.6851713f,true)==0x00060018u);
        queries.grid={0,0,16,16};
        assert(SourceSceneQueries::edgeCrossing({0,0},{1,0},{1,0},{1,1}));
        assert(!SourceSceneQueries::edgeCrossing({0,0},{2,0},{1,0},{3,0}));
        const auto wall=rectangle(2,0,7,16,9);
        queries.volumes.push_back({wall,0xffffffff,4,0});
        std::vector<bool> enabled(queries.volumes.size(),true);
        // Native low-X-grid shortcut ignores the intervening wall when visible.
        assert(queries.lineOfSight({2,0,2},{2,0,14},2,3,enabled));
        assert(!queries.lineOfSight({2,0,2},{2,0,14},2,2,enabled));
        assert(queries.lineOfSight({2,0,2},{2,0,14},4,2,enabled));
        enabled.back()=false;assert(queries.lineOfSight({2,0,2},{2,0,14},2,2,enabled));
        const ScriptVolume circle{2,3,{{0,99,0},{1,0,0}}};
        assert(SourceSceneQueries::segmentHits(circle,{-2,0},{2,0}));
        assert(SourceSceneQueries::segmentHits(circle,{-2,1},{2,1}));
        assert(!SourceSceneQueries::segmentHits(circle,{-2,0},{-1,0}));
        assert(!SourceSceneQueries::segmentHits(circle,{-.5f,0},{.5f,0}));
        if(argc>1) {
            ScriptGame game{std::filesystem::path(argv[1])};SourceGui gui;
            const auto bind=[&](const std::string& name,jpc::Interpreter::Native native) {
                for(const auto& method:game.package.methods)if(method.owner==game.package.classIndex("ed/base/External")&&method.name==name) {
                    game.vm.bind("ed/base/External",name,method.signature,std::move(native));return;
                }
                throw std::runtime_error("Missing test native: "+name);
            };
            for(const auto* name:SourceGui::Setters)bind(name,[&,name](auto& vm,auto,const auto& args){return gui.native(vm,name,args);});
            for(const auto* name:{"guiSysCreate","guiItemCreate","guiSysGetFlag","guiItemGetDataID","guiSysAtOffset","guiSysSet"})
                bind(name,[&,name](auto& vm,auto,const auto& args){return gui.native(vm,name,args);});
            for(const auto* name:{"platform","checkRumbleAbility"})bind(name,[](auto&,auto,const auto&){return jpc::Value::integer(0);});
            int milliseconds=0;
            bind("getMilliseconds",[&](auto&,auto,const auto&){return jpc::Value::integer(milliseconds);});
            bind("sndMgrGetStereo",[&](auto&,auto,const auto&){return jpc::Value::integer(gui.stereo);});
            bind("sndMgrGetMasterVolume",[&](auto&,auto,const auto&){return jpc::Value::integer(gui.masterVolume);});
            bind("sndMgrSetStereo",[&](auto&,auto,const auto& args){gui.stereo=args[0].integer();return jpc::Value{};});
            bind("sndMgrSetMasterVolume",[&](auto&,auto,const auto& args){gui.masterVolume=args[0].integer();return jpc::Value{};});
            std::vector<std::pair<std::string,std::vector<int>>> unavailableAudio;
            std::vector<std::array<int32_t,5>> cameraRequests;
            bind("controlCamera",[&](auto&,auto,const auto& args){
                std::array<int32_t,5> request{};for(unsigned i=0;i<5;++i)request[i]=args[i].integer();
                cameraRequests.push_back(request);return jpc::Value{}; // Presentation request-only GUI fixture.
            });
            bind("sndMgrGetSNDID",[&](auto&,auto,const auto& args){
                unavailableAudio.push_back({"sndMgrGetSNDID",{args[0].integer()}});
                return jpc::Value::integer(-1); // GUI-only fixture has no audio asset bank.
            });
            bind("sndMgrPlaySoundVol",[&](auto&,auto,const auto& args){
                if(args[0].integer()!=-1)throw std::runtime_error("GUI test unexpectedly received an available audio clip");
                unavailableAudio.push_back({"sndMgrPlaySoundVol",{args[0].integer(),args[1].integer()}});
                return jpc::Value::integer(-1); // Native unavailable clip/voice path.
            });
            game.vm.invoke(815);
            const auto initializationSteps=game.vm.steps;
            const auto allocated=[](const auto& table){return std::count_if(table.begin(),table.end(),[](const auto& r){return r.flags&8;});};
            assert(allocated(gui.systems)==49);assert(allocated(gui.items)==187);
            assert(!unavailableAudio.empty());
            // Execute the original wrapper used by InvSlot.setIconType, with
            // known source inventory atlas IDs; only presentation state changes.
            const auto beforeFlags=gui.items[0].flags;
            game.vm.invoke(game.package.methodIndex("Gui","itemSetPartUVUsingPart","(III)V"),0,
                {jpc::Value::integer(0),jpc::Value::integer(0x0b142244),jpc::Value::integer(12)});
            assert(gui.items[0].properties.at("guiItemSetPartUVUsingPart")==std::vector<int32_t>({0x0b142244,12}));
            assert(gui.items[0].flags==beforeFlags);
            game.vm.statics[309]=jpc::Value::integer(0x616b0100);
            const auto inventoryInterface=game.vm.makeObject(game.package.classIndex("InterfaceInventory"));
            const auto background=game.vm.invoke(game.package.methodIndex("InterfaceInventory","updateBackground","()I"),inventoryInterface).integer();
            game.vm.invoke(game.package.methodIndex("InterfaceInventory","setBackground","()V"),inventoryInterface);
            assert((cameraRequests.back()==std::array<int32_t,5>{{3,background,0,1,2}}));
            milliseconds=100;
            game.vm.invoke(1147,0,{jpc::Value::integer(0),jpc::Value::integer(3)});
            assert(game.vm.staticField(48).integer()==1);assert(game.vm.invoke(369).integer()==100);
            milliseconds=600;assert(game.vm.invoke(369).integer()==100);
            game.vm.invoke(1147,0,{jpc::Value::integer(3),jpc::Value::integer(0)});
            assert(game.vm.staticField(48).integer()==0);assert(game.vm.invoke(369).integer()==100);
            milliseconds=625;assert(game.vm.invoke(369).integer()==125);
            auto state=gui.snapshot();SourceGui restored;restored.restore(state);assert(restored.snapshot()==state);
            state.push_back(0);bool rejected=false;try{restored.restore(state);}catch(const std::exception&){rejected=true;}assert(rejected);
            std::cout<<"Original Main815 completed; GUI systems49/items187; "<<initializationSteps<<" source steps\n";
        }
        std::cout<<"Original visibility/LOS and progress tests passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
