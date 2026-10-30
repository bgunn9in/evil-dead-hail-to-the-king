#pragma once
#include "jpc.h"
#include <array>

// Exact original EXE/JPC actor resources; level bits1=A,2=B.
namespace edactors {
struct ModelDefinition {
    int type,capacity;const char* name;const char* scriptClass;
    uint32_t mesh,skeleton,texture;uint8_t levels;
};
inline constexpr std::array<ModelDefinition,30> Models{{
    {0,1,"ash","Ash",0x00024000,0x00824000,0x08024000,3},
    {2,4,"skullbat","SkullBat",0x04482000,0x04c82000,0x08482000,3},
    {4,4,"deadite","Deadite",0x040c6000,0x048c6000,0x080c6000,3},
    {5,1,"treeboss","TreeBoss",0x024e2000,0x02ce2000,0x084e2000,1},
    {6,4,"skeleton","Skeleton",0x06494000,0x06c94000,0x08494000,1},
    {7,4,"troopknife","TroopKnife",0x06318000,0x06b18000,0x08318000,1},
    {8,4,"troopclaw","TroopClaw",0x06304000,0x06b04000,0x08304000,1},
    {9,4,"trooprange","TroopRange",0x06300000,0x06b00000,0x08300000,1},
    {10,1,"troopleaderboss","TroopLeaderBoss",0x02496000,0x02c96000,0x08496000,1},
    {11,4,"hellbillyfat","HellbillyFat",0x061ca000,0x069ca000,0x081ca000,1},
    {12,4,"hellbillythin","HellbillyThin",0x061d8000,0x069d8000,0x081d8000,1},
    {13,4,"demonpig","DemonPig",0x063cc000,0x06bcc000,0x083cc000,1},
    {14,1,"matriarchboss","MatriarchBoss",0x02310000,0x02b10000,0x08310000,1},
    {17,1,"deadash","DeadAsh",0x020c0000,0x028c0000,0x080c0000,1},
    {18,1,"anniezombie","AnnieZombie",0x02032000,0x02832000,0x08032000,1},
    {34,1,"treearm","TreeArm",0x024c0000,0x02cc0000,0x084c0000,1},
    {35,1,"headlessskeleton","HeadlessSkeleton",0x06082000,0x06882000,0x08082000,1},
    {19,1,"badash","BadAsh",0x06040000,0x06840000,0x08040000,2},
    {23,4,"guildmember","GuildMember",0x06198000,0x06998000,0x08198000,2},
    {24,4,"merchant","Merchant",0x06326000,0x06b26000,0x08326000,2},
    {26,1,"wizardboss","WizardBoss",0x025b2000,0x02db2000,0x085b2000,2},
    {27,1,"mudgolem","MudGolem",0x0230c000,0x02b0c000,0x0830c000,2},
    {28,4,"armoryskeleton","ArmorySkeleton",0x06480000,0x06c80000,0x08480000,2},
    {29,1,"armoryboss","ArmoryBoss",0x02002000,0x02802000,0x08002000,2},
    {31,4,"netherpet","NetherPet",0x0635e000,0x06b5e000,0x0835e000,2},
    {33,1,"cthulhu","Cthulhu",0x020a8000,0x028a8000,0x080a8000,2},
    {38,1,"captain","Captain",0x0608c000,0x0688c000,0x0808c000,2},
    {39,1,"pumpkeeper","PumpKeeper",0x063d4000,0x06bd4000,0x083d4000,2},
    {31,4,"netherpet_06360000","NetherPet",0x06360000,0x06b60000,0x08360000,2},
    {31,4,"netherpet_06362000","NetherPet",0x06362000,0x06b62000,0x08362000,2},
}};
inline constexpr std::array<int,17> SupportedTypes{{0,2,4,5,6,7,8,9,10,11,12,13,14,17,18,34,35}};
inline const ModelDefinition& defaultModel(int type) {
    for(const auto& model:Models)if(model.type==type)return model;
    throw jpc::Error("Unsupported original actor type: "+std::to_string(type));
}
inline const char* actorName(int type){return defaultModel(type).name;}
inline int actorCapacity(int type){return defaultModel(type).capacity;}
inline uint32_t defaultTexture(int type){return defaultModel(type).texture;}
} // namespace edactors
