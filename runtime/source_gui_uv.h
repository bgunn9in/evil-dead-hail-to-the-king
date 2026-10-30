#pragma once
#include "state_bytes.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <utility>

namespace edguiuv {
using Key=std::pair<uint32_t,int>;
struct Part {std::vector<std::array<float,2>> uv;std::vector<uint16_t> indices;};
class Layout {
public:
    uint64_t fingerprint=1469598103934665603ull;
    std::map<uint32_t,std::vector<Part>> models;
    static Layout decode(const std::vector<uint8_t>& bytes) {
        if(bytes.size()>16*1024*1024)throw std::runtime_error("Excessive original GUI UV metadata");
        statebytes::Reader in{bytes};for(char c:std::string("EGUIUV1\0",8))if(in.get<uint8_t>()!=uint8_t(c))throw std::runtime_error("Invalid original GUI UV metadata");
        Layout result;for(auto byte:bytes){result.fingerprint^=byte;result.fingerprint*=1099511628211ull;}
        const auto count=in.count(1000);if(!count)throw std::runtime_error("Empty original GUI UV metadata");
        for(unsigned n=0;n<count;++n){const auto id=in.get<uint32_t>();const auto parts=in.count(256);
            if((id>>24)!=11||!parts||result.models.count(id))throw std::runtime_error("Invalid original GUI UV model");
            auto& model=result.models[id];for(unsigned i=0;i<parts;++i){Part part;const auto vertices=in.count(65536),indices=in.count(1000000);
                if(!vertices||indices%3)throw std::runtime_error("Invalid original GUI UV part");
                for(unsigned j=0;j<vertices;++j)part.uv.push_back({in.get<float>(),in.get<float>()});
                for(unsigned j=0;j<indices;++j){const auto ordinal=in.get<uint16_t>();if(ordinal>=vertices)throw std::runtime_error("Original GUI UV index is out of range");part.indices.push_back(ordinal);}
                model.push_back(std::move(part));}
        }in.end();return result;
    }
    static std::shared_ptr<const Layout> load(const std::filesystem::path& path) {
        std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Original GUI UV metadata is unavailable");
        const auto size=input.tellg();if(size<0||uint64_t(size)>16*1024*1024)throw std::runtime_error("Invalid original GUI UV metadata size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));input.seekg(0);if(!bytes.empty()&&!input.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Truncated original GUI UV metadata");
        return std::make_shared<const Layout>(decode(bytes));
    }
    const Part& at(Key key)const {const auto model=models.find(key.first);
        if(model==models.end()||key.second<0||size_t(key.second)>=model->second.size())throw std::runtime_error("Original GUI UV part is unavailable");return model->second[size_t(key.second)];}
};
} // namespace edguiuv
