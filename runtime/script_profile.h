#pragma once
#include "jpc.h"
#include <array>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#endif

// These host roles are verified declaration slots, not recovered field names.
// The VM still executes every operand from the untouched original package.
enum class HostFieldRole { InvBook7, InvBookText1, InventorySlots, InventoryAcceptedFlags,
    InventoryExcludedFlags, InventoryGui, GenericNpc30, ScenePendingPortal, ScenePortalIndex, ScenePortalSound };
struct ScriptProfile {
    bool dreamcast=false;
    explicit ScriptProfile(const jpc::Package& package) {
#ifdef _WIN32
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw jpc::Error("Cannot initialize script package SHA256");
        std::array<unsigned char,32> digest{};
        const auto status=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(package.data.data()),ULONG(package.data.size()),digest.data(),ULONG(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm,0);
        if(status<0)throw jpc::Error("Cannot verify script package SHA256");
        const char* digits="0123456789abcdef";std::string hash;
        for(auto c:digest){hash+=digits[c>>4];hash+=digits[c&15];}
        dreamcast=hash=="1ca3048f0e24130a2edbf8fcfd91001f3cd70aac522224372df4ef12530d7085"||
                  hash=="e47c2251da91069dcec1318cce62ae7791cba8c4277523a8ffd1fa2ca9920391";
        if(!dreamcast&&(package.fieldCount==1655||package.fieldCount==1659))
            throw jpc::Error("Unrecognized Dreamcast script package SHA256");
#else
        // Windows qualification first. Never infer a console ABI from extension or table sizes.
        if(package.fieldCount==1655||package.fieldCount==1659)
            throw jpc::Error("Original Dreamcast package SHA256 verification requires Windows");
#endif
    }
    uint32_t field(HostFieldRole role) const {
        // Exact original-SHA profiles; each pair is aligned_access_bijection in script-map.json.
        static constexpr uint32_t pc[]={897,899,996,997,998,1000,1251,1356,1357,1359};
        static constexpr uint32_t dc[]={896,898,995,996,997,999,1183,1288,1289,1291};
        return (dreamcast?dc:pc)[unsigned(role)];
    }
};
