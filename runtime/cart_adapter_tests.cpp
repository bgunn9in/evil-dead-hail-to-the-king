#include "cart_adapter.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>

namespace fs=std::filesystem;
using savefile::Bytes;
static Bytes raw(const fs::path& path){std::ifstream input(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(input),{});}
static void plain(const fs::path& path,const Bytes& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));assert(out.good());}
template<class F>static void fails(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}assert(failed);}

int main(int argc,char** argv) {
    try {
        const auto base=fs::absolute(argc>1?fs::path(argv[1]):fs::current_path()).lexically_normal();assert(fs::is_directory(base));
        fs::path directory;std::random_device random;
        for(unsigned i=0;i<16&&directory.empty();++i){const auto candidate=base/("cart-test-"+std::to_string(random())+"-"+std::to_string(random()));if(fs::create_directory(candidate))directory=candidate;}
        assert(!directory.empty()&&directory.parent_path()==base);
        edcart::Adapter cart(directory,128);int validated=0,saved=0,loaded=0;Bytes world{'o','k','1'};
        edcart::Hooks hooks;
        hooks.validate=[&](const Bytes& bytes){++validated;if(bytes.size()<2||bytes[0]!='o'||bytes[1]!='k')throw std::runtime_error("Full World validator rejected input");return edcart::Preview{"ak01",12.5};};
        hooks.save=[&]{++saved;return world;};hooks.load=[&](const Bytes& bytes){++loaded;world=bytes;};
        assert(cart.present()==1&&cart.freeBlocks()==1024&&cart.countValidSlots()==1&&cart.countValidCardSlots()==8&&!cart.done());
        assert(cart.queue(edcart::Operation::Save,0)==1&&cart.pending()&&!cart.done()&&saved==0);
        assert(cart.queue(edcart::Operation::Delete,1)==0&&cart.pending());
        auto result=cart.process(hooks);assert(result&&result->success&&cart.done()&&!cart.error()&&saved==1);
        assert(fs::exists(directory/"quicksave.eds")&&!fs::exists(directory/"slot0.eds")&&savefile::read(directory,"quicksave.eds",128)==world);
        assert(cart.checkValidSlot(0)==2&&cart.catalog(0).preview.room=="ak01"&&cart.countValidCardSlots()==7);
        assert(!cart.process(hooks));
        savefile::atomicWrite(directory,"slot1.eds",Bytes{'n','o'},128); // Valid envelope, invalid World/VM.
        auto damaged=raw(directory/"quicksave.eds");damaged.back()^=1;plain(directory/"slot2.eds",damaged);
        assert(cart.queue(edcart::Operation::Access));assert(cart.process(hooks)->success);
        assert(cart.checkValidSlot(0)==2&&cart.checkValidSlot(1)==0&&cart.checkValidSlot(2)==0&&cart.countValidCardSlots()==7);
        const auto before=world;assert(cart.queue(edcart::Operation::Load,1)&&loaded==0);
        result=cart.process(hooks);assert(!result->success&&cart.error()==4&&cart.done()&&loaded==0&&world==before);
        assert(cart.queue(edcart::Operation::Load,0));
        auto pending=cart.checkpoint();assert(pending.pending&&!pending.completed);
        auto broken=pending;broken.slots[1].status=2;fails([&]{cart.restore(broken);});assert(cart.pending());
        assert(cart.process(hooks)->success&&loaded==1&&!cart.error());
        cart.restore(pending);assert(cart.pending()&&!cart.done());assert(cart.process(hooks)->success&&loaded==2);
        // A file can change after cataloging. A new failed read/validation
        // invalidates its old status2 instead of advertising stale validity.
        savefile::atomicWrite(directory,"quicksave.eds",Bytes{'n','o'},128);
        assert(cart.queue(edcart::Operation::Load,0));assert(!cart.process(hooks)->success&&cart.checkValidSlot(0)==0);
        savefile::atomicWrite(directory,"quicksave.eds",before,128);
        assert(cart.queue(edcart::Operation::Access));assert(cart.process(hooks)->success&&cart.checkValidSlot(0)==2);
        // A transactional host-load failure must never become a successful native completion.
        auto failedLoad=hooks;failedLoad.load=[](const Bytes&){throw std::runtime_error("Destination assets unavailable");};
        assert(cart.queue(edcart::Operation::Load,0));assert(!cart.process(failedLoad)->success&&cart.error()==4&&world==before);
        // A failed serializer/full validator/write retains the previous file.
        const auto originalFile=raw(directory/"quicksave.eds");world={'n','o'};
        assert(cart.queue(edcart::Operation::Save,0));assert(!cart.process(hooks)->success&&raw(directory/"quicksave.eds")==originalFile);
        world=before;auto oversized=hooks;oversized.save=[] {return Bytes(129,'o');};
        assert(cart.queue(edcart::Operation::Save,0));assert(!cart.process(oversized)->success&&raw(directory/"quicksave.eds")==originalFile);
        const auto permissions=fs::status(directory/"quicksave.eds").permissions();
        fs::permissions(directory/"quicksave.eds",permissions&~(fs::perms::owner_write|fs::perms::group_write|fs::perms::others_write));
        assert(cart.queue(edcart::Operation::Save,0));assert(!cart.process(hooks)->success&&raw(directory/"quicksave.eds")==originalFile);
        fs::permissions(directory/"quicksave.eds",permissions);
        plain(directory/"slot3.eds",Bytes{'f','o','r','e','i','g','n'});const auto foreign=raw(directory/"slot3.eds");
        assert(cart.queue(edcart::Operation::Delete,3));assert(!cart.process(hooks)->success&&raw(directory/"slot3.eds")==foreign);
        assert(cart.queue(edcart::Operation::Delete,0));assert(cart.process(hooks)->success&&!fs::exists(directory/"quicksave.eds")&&cart.checkValidSlot(0)==0);
        assert(raw(directory/"slot1.eds")==savefile::detail::encode(Bytes{'n','o'},128));
        fails([&]{cart.queue(edcart::Operation::Save,8);});fails([&]{edcart::Adapter::filename(unsigned(-1));});
        // Only explicitly named files within this fresh verified child are removed.
        for(const auto* name:{"slot1.eds","slot2.eds","slot3.eds"})assert(fs::remove(directory/name));
        assert(fs::is_empty(directory)&&fs::remove(directory));
        std::cout<<"Cart8slots/real boundedIO/VMvalidation/deferredcompletion/rollback/delete ownership: passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
