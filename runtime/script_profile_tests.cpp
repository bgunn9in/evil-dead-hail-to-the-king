#include "script_profile.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
int main(int argc,char** argv){assert(argc==2);const std::filesystem::path root=argv[1];
 for(const auto& file:{root/"scripts/levelA.jpc",root/"levelB/scripts/levelB.jpc"}) {auto p=jpc::Package::load(file);ScriptProfile profile(p);assert(profile.dreamcast);assert(profile.field(HostFieldRole::InventorySlots)==995);assert(profile.field(HostFieldRole::ScenePendingPortal)==1288);bool ambiguous=false;try{p.classIndex("Chainsaw");}catch(const jpc::Error&){ambiguous=true;}assert(ambiguous);p.data.back()^=1;bool rejected=false;try{ScriptProfile changed(p);}catch(const jpc::Error&){rejected=true;}assert(rejected);}
 std::cout<<"Original DC A/B exact SHA roles; modified DC refused; duplicate Chainsaw remains ambiguous\n";
}
