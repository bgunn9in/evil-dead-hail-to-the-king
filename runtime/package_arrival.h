#pragma once
#include "script_game.h"
#include "episode_portals.h"
#include "source_vfx.h"

struct PackageRequest {
    uint32_t sourceSceneId=0,destinationId=0,camera=0;
    int32_t marker=-1;
    std::array<float,3> rotationXYZ{};
};
namespace edpackage {
inline std::string sceneName(uint32_t id) {
    const unsigned number=(id>>8)&255;
    if((id&255)||((id>>24)!='a'&&(id>>24)!='b')||((id>>16)&255)<'a'||((id>>16)&255)>'z'||number>99)
        throw jpc::Error("Invalid original package destination");
    return std::string{char(id>>24),char(id>>16),char('0'+number/10),char('0'+number%10)};
}
inline bool restart(uint32_t destination) {return (destination&0xffffff00u)==0x61610100u||(destination&0xffffff00u)==0x62620100u;}
inline bool reload(uint32_t source,uint32_t destination) {return ((source^destination)&0xff000000u)||restart(destination);}
inline bool restorePlayer(uint32_t source,uint32_t destination) {
    return (destination==0x626d0100u&&(source>>24)=='a')||(destination==0x616b0100u&&source==0x62620100u);
}
}
// Only process-global native values survive the fresh script/actor context.
struct PackageArrival {
    PackageRequest request;
    std::string scene;
    ScriptGame::PlayerTransferBuffer player;
    double rawSeconds=0;
    bool liveClock=false;uint64_t wallStartNS=0;
    uint32_t randomIndex=0;bool randomReady=false;
    std::array<uint32_t,64> randomTable{};
    std::vector<uint8_t> screen;
    int32_t sourceFloorCache=-1;
    int masterVolume=100,stereo=1;
    int32_t turnAmount=8;
    edvfx::SourceVfx::Globals vfxGlobals;
};
