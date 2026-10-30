#include "save_file.h"
#include <fstream>
#include <iostream>
#include <iterator>
#undef NDEBUG
#include <cassert>

namespace fs=std::filesystem;
using savefile::Bytes;
Bytes raw(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(input),{});
}
void plain(const fs::path& path,const Bytes& data) {
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
    assert(output.good());
}
template<class F> void fails(F operation) {
    bool failed=false;
    try {operation();}catch(const std::exception&){failed=true;}
    assert(failed);
}
std::vector<fs::path> children(const fs::path& directory) {
    std::vector<fs::path> result;
    for(const auto& entry:fs::directory_iterator(directory))result.push_back(entry.path().filename());
    std::sort(result.begin(),result.end());
    return result;
}
int main(int argc,char** argv) {
    const auto base=fs::absolute(argc>1?fs::path(argv[1]):fs::current_path()).lexically_normal();
    assert(fs::is_directory(base));
    std::random_device random;
    fs::path directory;
    for(unsigned attempt=0;attempt<16&&directory.empty();++attempt) {
        const auto candidate=base/("save-file-test-"+std::to_string(random())+"-"+std::to_string(random()));
        if(fs::create_directory(candidate))directory=candidate;
    }
    assert(!directory.empty()&&directory.parent_path()==base);
    const Bytes first{'o','l','d'},second{'n','e','w'},foreign{'o','r','i','g','i','n','a','l'};
    savefile::atomicWrite(directory,"slot1.sav",first);
    assert(savefile::read(directory,"slot1.sav")==first);
    const auto before=raw(directory/"slot1.sav");
    assert(before.size()==savefile::HeaderBytes+first.size());
    assert(savefile::detail::checksum(Bytes{'1','2','3','4','5','6','7','8','9'})==0xcbf43926u);
    plain(directory/"slot1.sav.tmp",foreign);
    savefile::atomicWrite(directory,"slot1.sav",second);
    assert(savefile::read(directory,"slot1.sav")==second);
    assert(raw(directory/"slot1.sav.tmp")==foreign);
    const auto prior=raw(directory/"slot1.sav");
    const auto priorChildren=children(directory);
    fails([&]{savefile::atomicWrite(directory,"slot1.sav",Bytes(129),128);});
    fails([&]{savefile::read(directory,"slot1.sav",1);});
    assert(raw(directory/"slot1.sav")==prior&&children(directory)==priorChildren);
    for(const auto& name:{"../original.bin","slot1.sav:stream","NUL","slot1.sav.","bad/name.sav"})
        fails([&]{savefile::atomicWrite(directory,name,second);});
    plain(directory/"original.bin",foreign);
    fails([&]{savefile::atomicWrite(directory,"original.bin",second);});
    assert(raw(directory/"original.bin")==foreign);
    auto corrupt=prior; corrupt.back()^=1; plain(directory/"corrupt.sav",corrupt);
    fails([&]{savefile::read(directory,"corrupt.sav");});
    savefile::atomicWrite(directory,"corrupt.sav",second);
    assert(savefile::read(directory,"corrupt.sav")==second);
    auto future=prior; future[8]=2; plain(directory/"future.sav",future);
    fails([&]{savefile::atomicWrite(directory,"future.sav",second);});
    fails([&]{savefile::read(directory,"future.sav");});
    assert(raw(directory/"future.sav")==future);
    plain(directory/"truncated.sav",Bytes(prior.begin(),prior.begin()+12));
    fails([&]{savefile::read(directory,"truncated.sav");});
    auto trailing=prior; trailing.push_back(0); plain(directory/"trailing.sav",trailing);
    fails([&]{savefile::read(directory,"trailing.sav");});
    const auto originalPermissions=fs::status(directory/"slot1.sav").permissions();
    fs::permissions(directory/"slot1.sav",originalPermissions&~(fs::perms::owner_write|fs::perms::group_write|fs::perms::others_write));
    fails([&]{savefile::atomicWrite(directory,"slot1.sav",first);});
    fs::permissions(directory/"slot1.sav",originalPermissions);
    assert(raw(directory/"slot1.sav")==prior);
#ifdef _WIN32
    const auto filesBeforeFailure=children(directory);
    const HANDLE locked=CreateFileW((directory/"slot1.sav").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    fails([&]{savefile::atomicWrite(directory,"slot1.sav",first);});
    assert(CloseHandle(locked));
    assert(raw(directory/"slot1.sav")==prior&&children(directory)==filesBeforeFailure);
#endif
    const auto unicode=directory/fs::u8path(u8"профиль");
    assert(fs::create_directory(unicode));
    savefile::atomicWrite(unicode,"slot1.sav",second);
    assert(savefile::read(unicode,"slot1.sav")==second);
    std::error_code linkError;
    fs::create_symlink(directory/"original.bin",directory/"linked.sav",linkError);
    if(!linkError) {
        fails([&]{savefile::read(directory,"linked.sav");});
        fails([&]{savefile::atomicWrite(directory,"linked.sav",second);});
        assert(raw(directory/"original.bin")==foreign);
    } else std::cout<<"File symlink fixture unavailable: "<<linkError.message()<<'\n';
    linkError.clear();
    fs::create_directory_symlink(unicode,directory/"linked-directory",linkError);
    if(!linkError) {
        fails([&]{savefile::read(directory/"linked-directory","slot1.sav");});
        fails([&]{savefile::atomicWrite(directory/"linked-directory","slot1.sav",first);});
        assert(savefile::read(unicode,"slot1.sav")==second);
    } else std::cout<<"Directory symlink fixture unavailable: "<<linkError.message()<<'\n';
    // Remove only this test's named files/links; never recursively delete a computed path.
    for(const auto& name:{"linked-directory","linked.sav","slot1.sav","slot1.sav.tmp","original.bin",
                         "corrupt.sav","future.sav","truncated.sav","trailing.sav"}) {
        std::error_code ignored;fs::remove(directory/name,ignored);
    }
    assert(fs::remove(unicode/"slot1.sav")&&fs::remove(unicode));
    assert(children(directory).empty()&&fs::remove(directory));
    std::cout<<"Save envelope, bounded read, exclusive temp and preserved previous file: passed\n";
}
