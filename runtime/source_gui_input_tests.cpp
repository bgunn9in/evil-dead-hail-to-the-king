#include "source_gui.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw jpc::Error("Pass an original levelA/levelB.jpc");
        const auto package=jpc::Package::load(argv[1]);jpc::Interpreter vm(package);SourceGui gui;
        const auto I=[](int value){return jpc::Value::integer(value);};
        auto call=[&](const char* name,std::initializer_list<jpc::Value> args){return gui.native(vm,name,args);};
        const auto pad=vm.makeObject(package.classIndex("MenMain"));
        using Event=std::array<int,4>;std::vector<Event> events;
        vm.bind("Pad","actionItem","(Led/base/EDBase;III)V",[&](auto&,auto,const auto& args){
            events.push_back({0,args[1].integer(),args[2].integer(),args[3].integer()});return jpc::Value{};
        });
        vm.bind("Pad","actionSys","(Led/base/EDBase;III)V",[&](auto&,auto,const auto& args){
            events.push_back({1,args[1].integer(),args[2].integer(),args[3].integer()});return jpc::Value{};
        });
        for(int system=0;system<2;++system) {
            assert(call("guiSysCreate",{I(2)}).integer()==system);
            gui.systems[system].pad=pad;
        }
        for(int item=0;item<4;++item) {
            assert(call("guiItemCreate",{I(2),I(item==3?1:0)}).integer()==item);
            gui.items[item].pad=pad;gui.items[item].flags|=16;
        }
        call("guiItemLink",{I(0),I(3),I(1),I(2)});
        call("guiItemLink",{I(1),I(3),I(2),I(2)});
        call("guiItemLink",{I(0),I(1),I(2),I(0)});
        call("guiSysLink",{I(0),I(1),I(1),I(0)});
        assert(gui.items[0].links[3]==1&&gui.items[1].links[2]==0&&gui.items[0].links[1]==2);
        assert(gui.systems[0].links[1]==1&&gui.systems[1].links[0]==0);
        call("guiSysSet",{I(0)});
        assert((events==std::vector<Event>{{1,97,0,0},{0,97,0,0}}));
        events.clear();call("guiSysSet",{I(0)});assert(events.empty());
        gui.input(vm,106);
        assert((events==std::vector<Event>{{0,106,0,0},{1,106,0,0},{0,98,0,0},{0,97,1,1}}));
        assert(gui.systems[0].selected==1&&!(gui.items[0].flags&4)&&(gui.items[1].flags&4));
        // Native navigation follows links through entries lacking bit0x10.
        gui.items[1].flags&=~16u;call("guiItemSelect",{I(0),I(0)});events.clear();gui.input(vm,106);
        assert(gui.systems[0].selected==2);
        assert((events==std::vector<Event>{{0,106,0,0},{1,106,0,0},{0,98,0,0},{0,97,2,2}}));
        // No item link: only current-item bit0x40 permits a system link.
        events.clear();gui.input(vm,104);assert(gui.activeSystem==0&&events.size()==2);
        gui.items[2].flags|=64;events.clear();gui.input(vm,104);
        assert(gui.activeSystem==1);
        assert((events==std::vector<Event>{{0,104,2,2},{1,104,0,0},{1,98,0,0},{1,97,1,0},{0,97,3,0}}));
        events.clear();gui.input(vm,111);
        assert((events==std::vector<Event>{{0,111,3,0},{1,111,1,0}}));
        // Input batch snapshots the original system even if a callback changes it.
        gui.setSystem(vm,0);events.clear();bool changed=false;
        vm.bind("Pad","actionItem","(Led/base/EDBase;III)V",[&](auto&,auto,const auto& args){
            events.push_back({0,args[1].integer(),args[2].integer(),args[3].integer()});
            if(args[1].integer()==111&&!changed){changed=true;gui.setSystem(vm,1);}return jpc::Value{};
        });
        gui.dispatchInput(vm,{111,112});
        assert(gui.activeSystem==1);
        assert((events[events.size()-2]==Event{{0,112,2,2}}));
        assert((events.back()==Event{{1,112,0,0}}));
        const auto state=gui.snapshot();SourceGui restored;restored.restore(state);assert(restored.snapshot()==state);restored.validateReferences(vm);
        auto damaged=state;damaged.push_back(0);bool rejected=false;
        try{restored.restore(damaged);}catch(const std::exception&){rejected=true;}assert(rejected&&restored.snapshot()==state);
        // Same numeric ref in a different restored VM must not validate merely
        // because that VM also owns an object in the corresponding slot.
        jpc::Interpreter wrongVm(package);wrongVm.makeObject(package.classIndex("java/lang/String"));
        rejected=false;try{restored.validateReferences(wrongVm);}catch(const jpc::Error&){rejected=true;}assert(rejected);
        call("guiItemLink",{I(0),I(3),I(1),I(2)});call("guiItemLink",{I(1),I(3),I(0),I(2)});
        gui.items[0].flags&=~16u;gui.items[1].flags&=~16u;gui.systems[0].selected=0;
        rejected=false;try{gui.navigate(vm,0,3);}catch(const jpc::Error&){rejected=true;}assert(rejected);
        std::cout<<"Source GUI focus, bilateral links, disabled-entry navigation, callback order/batch capture and state passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
