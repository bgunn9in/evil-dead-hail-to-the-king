#include "script_game.h"
#include "cart_adapter.h"
#include "ui_strings.h"
#include "episode_portals.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <memory>
namespace fs=std::filesystem;

struct CartWorld {
    ScriptGame game;
    explicit CartWorld(const fs::path& script):game(script,false) {
        auto bind=[&](const std::string& name,jpc::Interpreter::Native callback) {
            for(const auto& method:game.package.methods)if(method.owner==game.package.classIndex("ed/base/External")&&method.name==name) {
                game.vm.bind("ed/base/External",name,method.signature,std::move(callback));return;
            }
            throw jpc::Error("Cart ABI import is missing");
        };
        bindCartNatives(bind);
    }
    const std::map<uint32_t,std::string>& sourceMenuStrings() const {
        static const std::map<uint32_t,std::string> strings{{0x31990960,"source card label"}};return strings;
    }
    struct TextFixture {
        bool translated=false;
        bool localized()const{return translated;}
        bool contains(uint32_t scope,uint32_t id)const{return scope==0x616b0100&&id==edcart::Adapter::cardTextResource();}
        std::string lookup(uint32_t,uint32_t)const{return "localized card label";}
    } texts;
    const TextFixture& sourceTexts()const{return texts;}
    struct SceneFixture {std::string id="ak01";};
    SceneFixture nativeScene()const{return {};}
    #include "world_cart.inl"
    jpc::Value call(const char* method,const char* signature,std::vector<jpc::Value> args={}) {
        return game.vm.invoke(game.package.methodIndex("Cart",method,signature),0,std::move(args));
    }
};

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Pass originalJPC and ownedtestparent");
        CartWorld world(argv[1]);const auto I=[](int value){return jpc::Value::integer(value);};
        assert(world.game.profile.dreamcast);
        assert(!world.sourceCart&&world.call("countValidSlots","()I").integer()==8);
        assert(world.call("countValidCardSlots","()I").integer()==1);
        assert(world.call("getFreeBlocks","(I)I",{I(0)}).integer()==edcart::Adapter::freeBlocks());
        assert(world.call("getFreeBlocks","(I)I",{I(1)}).integer()==-1);
        world.call("end","()V");assert(!world.sourceCart);
        assert(world.sourceCartText(0xffff0100u)==edui::text("save files",false));
        for(unsigned slot=1;slot<8;++slot)assert(world.sourceCartText(0xffff0100u|slot)==std::string{});
        world.texts.translated=true;assert(world.sourceCartText(0xffff0100u)==edui::text("save files",true));
        assert(world.sourceCartText(0xffff0200u)==edui::text("empty",true));
        world.texts.translated=false;
        assert(world.sourceCartText(0xffff0200u)==edui::text("empty",false));
        const auto base=fs::absolute(argv[2]).lexically_normal();assert(fs::is_directory(base));
        std::random_device random;fs::path directory;
        for(unsigned i=0;i<16&&directory.empty();++i){const auto candidate=base/("cart-glue-test-"+std::to_string(random()));if(fs::create_directory(candidate))directory=candidate;}
        assert(!directory.empty()&&directory.parent_path()==base);world.configureSourceCart(directory);
        int validated=0,saved=0,loaded=0;savefile::Bytes payload{'o','k'};
        edcart::Hooks hooks;
        hooks.validate=[&](const auto& bytes){++validated;assert(bytes==payload);return edcart::Preview{"ak01",3661.75};};
        hooks.save=[&]{++saved;return payload;};hooks.load=[&](const auto& bytes){assert(bytes==payload);++loaded;};
        world.call("begin","()V");assert(world.sourceCart&&world.sourceCart->pending()&&validated==0);
        assert(world.call("accessCard","(I)Z",{I(0)}).integer()==1);
        assert(world.call("accessCardDone","(I)Z",{I(0)}).integer()==0);
        assert(world.sourceCart->process(hooks)->success&&validated==0);
        assert(world.call("accessCardDone","(I)Z",{I(0)}).integer()==1);
        assert(world.call("saveGame","(I)Z",{I(0)}).integer()==1&&saved==0);
        assert(!world.call("saveGameDone","(I)Z",{I(0)}).integer());
        auto checkpoint=world.sourceCartCheckpoint();assert(checkpoint->pending);
        assert(world.sourceCart->process(hooks)->success&&saved==1);
        assert(world.call("saveGameDone","(I)Z",{I(0)}).integer()==1&&world.call("error","()I").integer()==0);
        assert(world.sourceCartText(0xffff0200u)=="ak01 01:01:01");
        const auto retained=savefile::read(directory,"quicksave.eds");
        assert(world.call("deleteGame","(I)Z",{I(0)}).integer()==1);
        assert(!world.sourceCart->pending()&&world.sourceCart->done()&&savefile::read(directory,"quicksave.eds")==retained);
        assert(world.call("countValidSlots","()I").integer()==8&&world.call("countValidCardSlots","()I").integer()==1);
        assert(world.call("loadGame","(I)Z",{I(0)}).integer()==1&&loaded==0);
        assert(world.call("deleteGame","(I)Z",{I(0)}).integer()==0&&world.sourceCart->pending());
        assert(world.sourceCart->process(hooks)->success&&loaded==1);
        // End cancels only queued work, not a completed filesystem operation.
        assert(world.call("saveGame","(I)Z",{I(1)}).integer());world.call("end","()V");
        assert(!world.sourceCart->pending()&&!world.sourceCart->done()&&saved==1&&!fs::exists(directory/"slot1.eds"));
        world.restoreSourceCartCheckpoint(checkpoint);assert(world.sourceCart->pending());world.call("end","()V");
        const auto file=savefile::read(directory,"quicksave.eds");
        bool rejected=false;try{world.call("format","()Z");}catch(const jpc::Error&){rejected=true;}assert(rejected&&savefile::read(directory,"quicksave.eds")==file);
        world.restoreSourceCartCheckpoint({});assert(!world.sourceCart&&world.sourceCartDirectory==directory);
        assert(fs::remove(directory/"quicksave.eds")&&fs::is_empty(directory)&&fs::remove(directory));
        std::cout<<"OriginalDC A/B Cart wrappers/8records/1card/19nativeABI/atomic overwrite/deferredIO/checkpoints/FFFFtext passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
