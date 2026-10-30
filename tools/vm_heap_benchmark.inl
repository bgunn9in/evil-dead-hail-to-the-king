// Private benchmark appended to a copy of main.cpp before its entry point.
// Calls the real World scene transaction with source portal metadata. This is
// a lifecycle fixture, not evidence that the player has earned those passages.
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib,"Psapi.lib")
#endif
namespace vm_heap_benchmark {
using Clock=std::chrono::steady_clock;
double millis(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::pair<uint64_t,uint64_t> memory(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX state{};state.cb=sizeof(state);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&state),sizeof(state)))
        throw std::runtime_error("Read private benchmark memory counters");
    return {uint64_t(state.PrivateUsage),uint64_t(state.WorkingSetSize)};
#else
    return {};
#endif
}
void blob(const fs::path& path,const std::vector<uint8_t>& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!out)throw std::runtime_error("Write private benchmark snapshot");}
void hostRoots(const fs::path& path,const World& world){std::set<uint32_t> roots;
    for(auto ref:{world.game.ash,world.game.level,world.game.scene,world.game.inventory})if(ref)roots.insert(ref);
    if(world.actors)for(const auto& [key,body]:world.actors->bodies()){(void)key;if(body.script)roots.insert(body.script);}
    for(const auto& record:world.sourceGui.systems)if((record.flags&8)&&record.pad)roots.insert(record.pad);
    for(const auto& record:world.sourceGui.items)if((record.flags&8)&&record.pad)roots.insert(record.pad);
    std::ofstream out(path);out<<"{\"refs\":[";bool first=true;for(auto ref:roots){if(!first)out<<',';first=false;out<<ref;}out<<"]}\n";
    if(!out)throw std::runtime_error("Write private benchmark roots");
}
int run(int argc,char** argv){
    if(argc<5||argc>6)throw std::runtime_error("Pass assets, NEW owned output, transition count, ordinary frames/transition and optional tour");
    const fs::path root=fs::absolute(fs::u8path(argv[1])),output=fs::absolute(fs::u8path(argv[2]));
    const unsigned transitions=unsigned(std::stoul(argv[3])),frames=unsigned(std::stoul(argv[4]));
    const bool tour=argc==6&&std::string(argv[5])=="tour";
    if(argc==6&&!tour)throw std::runtime_error("Unknown private lifecycle benchmark route");
    if(transitions>2000||frames>1000||!transitions||!frames||output.parent_path()!=fs::absolute("build")||fs::exists(output))
        throw std::runtime_error("Invalid bounded private benchmark output/count");
    if(!fs::create_directory(output))throw std::runtime_error("Create fresh private benchmark output");
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize read-only lifecycle benchmark");struct Quit{~Quit(){SDL_Quit();}} quit;
    World world(root,nullptr,presentMovieHeadless,true);std::cout<<std::unitbuf;
    const std::string first=world.levelBPackage?"bm01":"ak01",second=world.levelBPackage?"bm02":"ak09";
    if(world.scene.id!=first)throw std::runtime_error("Private benchmark must start at source playable entry");
    const auto persistentAsh=world.game.ash,persistentLevel=world.game.level,persistentInventory=world.game.inventory;
    std::ofstream csv(output/"metrics.csv");csv<<"iteration,scene,source_tick,objects,vm_bytes,world_bytes,private_bytes,working_set_bytes,switch_ms,step_median_us,vm_snapshot_ms,world_snapshot_ms,arena_generation\n";
    auto sample=[&](unsigned iteration,double switchTime,const std::vector<double>& frameTimes){
        if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);
        const auto begin=Clock::now();const auto snapshot=world.game.vm.snapshot();const double snapshotTime=millis(begin);
        const auto saveBegin=Clock::now();const auto saved=world.saveStateBytes();const double saveTime=millis(saveBegin);
        uint32_t count=0;for(unsigned b=0;b<4;++b)count|=uint32_t(snapshot.at(12+b))<<(b*8);
        auto sorted=frameTimes;std::sort(sorted.begin(),sorted.end());const auto [privateBytes,workingSet]=memory();
        csv<<iteration<<','<<world.scene.id<<','<<world.sourceTick<<','<<count<<','<<snapshot.size()<<','<<saved.size()<<','<<privateBytes<<','<<workingSet<<','
            <<switchTime<<','<<(sorted.empty()?0:sorted[sorted.size()/2])<<','<<snapshotTime<<','<<saveTime<<','<<world.progressArenaGeneration<<'\n';csv.flush();
        if(iteration==0||iteration==1||iteration%25==0||iteration==transitions){
            auto name=std::string("snapshot-")+std::to_string(iteration);blob(output/(name+".vm"),snapshot);hostRoots(output/(name+".roots.json"),world);
        }
        if(iteration==0||iteration%25==0||iteration==transitions)std::cout<<"Lifecycle heap "<<(world.levelBPackage?'B':'A')<<' '<<iteration<<'/'<<transitions<<": "<<count<<" objects, "<<saved.size()<<" save bytes, "<<privateBytes<<" private bytes\n";
    };
    sample(0,0,{});
    for(unsigned iteration=1;iteration<=transitions;++iteration){
        const std::string destination=tour?world.areas[(iteration-1)%world.areas.size()]:(world.scene.id==first?second:first);
        const auto begin=Clock::now();
        if(tour){
            // Same full original end/create/activate path as source_scene_flow_tests;
            // authored exported spawn, not a claimed connected walking route.
            world.endEpisodeScene();Scene next=loadScene(root/destination);Actor placed=resetActor(next,0);
            std::vector<GateState> gates(next.doors.size());world.enterSourceScene(next,placed,gates);
            world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(gates);
        }else{
            auto selected=std::find_if(world.scene.portalData.portals.begin(),world.scene.portalData.portals.end(),[&](const auto& p){return p.resolved&&edpackage::sceneName(p.destinationId)==destination;});
            if(selected==world.scene.portalData.portals.end())throw std::runtime_error("No original portal in benchmark pair");
            const auto portal=*selected;const auto door=std::find_if(world.scene.doors.begin(),world.scene.doors.end(),[&](const auto& d){return d.portalIndex==portal.index;});
            if(door==world.scene.doors.end())throw std::runtime_error("Original portal has no World transaction record");
            if(!world.switchScene({episode_portal_detail::sceneId(world.scene.id),portal.destinationId,portal.camera,int32_t(portal.marker),portal.rotationXYZ},nullptr,int(door-world.scene.doors.begin())))
                throw std::runtime_error("Source scene transaction did not commit");
        }
        const double switchTime=millis(begin);if(world.scene.id!=destination)throw std::runtime_error("Source scene identity differs");
        std::vector<double> frameTimes;
        for(unsigned frame=0;frame<frames;++frame){const auto tick=Clock::now();world.step({},false,false,.033f);frameTimes.push_back(millis(tick)*1000);
            if(world.scriptFault||world.scene.id!=destination||world.pendingSceneSwitch)throw std::runtime_error("Lifecycle benchmark encountered another source scene request or fault");}
        if(world.game.ash!=persistentAsh||world.game.level!=persistentLevel||world.game.inventory!=persistentInventory)
            throw std::runtime_error("Persistent player/level/inventory numeric references changed");
        sample(iteration,switchTime,frameTimes);
    }
    std::cout<<"Lifecycle fixture complete; no health, inventory, mission, enemy or AI writes; "
        <<(tour?"source end/create/activate fixtures are not a story route.":"original portal requests bypass player interaction/navigation.")<<'\n';
    return 0;
}
}
int main(int argc,char** argv){try{return vm_heap_benchmark::run(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
