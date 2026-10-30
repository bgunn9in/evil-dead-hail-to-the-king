#include "source_gui_presentation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw jpc::Error("Pass original JPC and generated GUI asset root");
        const auto p=jpc::Package::load(argv[1]);jpc::Interpreter vm(p);SourceGui gui;
        for(const auto& method:p.methods)if(p.classes[size_t(method.owner)].name=="ed/base/External"&&
            (SourceGui::setter(method.name)||method.name=="guiSysCreate"||method.name=="guiItemCreate"||method.name=="guiSysGetFlag")) {
            const auto name=method.name;vm.bind("ed/base/External",name,method.signature,[&,name](auto& machine,auto,const auto& args){return gui.native(machine,name,args);});
        }
        vm.initialize("GameOver");const auto receiver=vm.makeObject(p.classIndex("GameOver"));
        vm.invoke(p.methodIndex("GameOver","<init>","()V"),receiver);vm.invoke(p.methodIndex("GameOver","initialize","()V"),receiver);
        edguipresentation::GameOver asset(argv[2]);assert(asset.sample(gui,vm).empty());
        vm.invoke(p.methodIndex("GameOver","enable","()V"),receiver);const auto active=asset.sample(gui,vm);
        assert(active.size()==1&&active[0].vertices.size()==6&&active[0].texture==0x0b942034&&active[0].color==0xffffffff);
        const auto first=active[0].vertices[0];
        assert(std::abs(first.x-7.016610145568848f/16)<1e-6f&&std::abs(first.y-(-1.5214999914169312f/12))<1e-6f&&first.z==.5f);
        assert(first.u==0.8268470168113708f&&first.v==0.9951581954956055f);
        const auto state=gui.snapshot();SourceGui copy;copy.restore(state);copy.validateReferences(vm);assert(asset.sample(copy,vm)[0].vertices[0].x==first.x);
        vm.invoke(p.methodIndex("GameOver","disable","()V"),receiver);assert(asset.sample(gui,vm).empty());
        std::cout<<"Original GameOver JPC visibility -> part0/texture0B942034 -> native20x640x480/rawUV -> hidden and exact GUI3 restore passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
