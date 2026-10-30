#include "script_game.h"
#include "script_scene.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);const std::filesystem::path root=argv[1];
    ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak01");
    const auto native=[](const std::filesystem::path& path) {
        std::ifstream stream(path,std::ios::binary);assert(stream);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),{});
    };
    ScriptScene geometry(native(root/"ak01/scene.native"));
    GroundPoint position{2.45914788f,0.0463946983f,8.35600605f};
    const auto marker=geometry.markers[33];float yaw=std::atan2(marker.x-position.x,marker.z-position.z);
    game.vm.bind("ed/base/External","actorInTrigger","(III)I",[&](auto&,auto,const auto& args){
        return jpc::Value::integer(geometry.inside(unsigned(args[2].integer()),position));
    });
    game.vm.bind("ed/base/External","actorIsFacingMarker","(IIII)I",[&](auto&,auto,const auto& args){
        return jpc::Value::integer(geometry.facing(unsigned(args[2].integer()),float(args[3].integer()),{position.x,position.z},yaw));
    });
    const auto empty=game.vm.snapshot();
    assert(game.interact()==116);const auto items=game.items();
    assert(items.size()==1&&items[0].itemID==119&&items[0].quantity==1&&items[0].slot==0);
    assert(game.vm.staticField(418).integer()==1&&game.mode==2);
    assert(game.interact()==0&&game.items().size()==1&&game.messageAsset==0x318a0804);
    game.enterScene("ak09");game.enterScene("ak01");
    assert(game.vm.staticField(418).integer()==1&&game.items().size()==1);
    const auto acquired=game.vm.snapshot();
    game.restore(empty);assert(game.items().empty()&&game.vm.staticField(418).integer()==0);
    game.restore(acquired);assert(game.items().size()==1&&game.vm.staticField(418).integer()==1);
    const auto unchanged=game.vm.snapshot();
    auto damaged=unchanged;damaged[20]^=1;bool rejected=false;
    try {game.vm.restore(damaged);}catch(const jpc::Error&){rejected=true;}
    assert(rejected&&game.vm.snapshot()==unchanged);
    try {
    const int fullHealthUse=game.useItem(119);
    std::cout<<"Full health item result: "<<fullHealthUse<<", items: "<<game.items().size()<<std::endl;
    assert(fullHealthUse==0&&game.items().size()==1);
    game.vm.setField(game.ash,13,jpc::Value::integer(50));
    const int healing=game.useItem(119);
    std::cout<<"Healing item result: "<<healing<<", HP: "<<game.vm.field(game.ash,13).integer()<<", items: "<<game.items().size()<<std::endl;
    assert(healing==1&&game.vm.field(game.ash,13).integer()==85&&game.items().empty());
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
    std::cout<<"Script game passed: source startup globals, inventory constructors, geometry, pickup, repeat and scene persistence\n";
}
