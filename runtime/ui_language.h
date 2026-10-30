#pragma once
#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <stdexcept>

enum class UiLanguage:uint8_t {English=0,Russian=1};
inline bool validUiLanguage(UiLanguage value){return value==UiLanguage::English||value==UiLanguage::Russian;}
inline bool russianUi(UiLanguage value){return value==UiLanguage::Russian;}
inline std::filesystem::path uiTexturePath(const std::filesystem::path& root,uint32_t id,UiLanguage language) {
    if(!validUiLanguage(language))throw std::runtime_error("Unsupported UI language");
    char name[32];std::snprintf(name,sizeof(name),"%08x.rgba",id);
    return root/"gui"/(!russianUi(language)&&(id==0x0b942034u||id==0x0b942054u)?"languages/en":"textures")/name;
}
inline std::filesystem::path uiInventoryPath(const std::filesystem::path& root,UiLanguage language) {
    if(!validUiLanguage(language))throw std::runtime_error("Unsupported UI language");
    return russianUi(language)?root/"inventory.bin":root/"gui/languages/en/inventory.bin";
}
