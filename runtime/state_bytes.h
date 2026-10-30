#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace statebytes {
using Bytes=std::vector<uint8_t>;
struct Writer {
    Bytes bytes;
    template<class T> void put(T value) {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>);
        if constexpr(std::is_floating_point_v<T>)
            if(!std::isfinite(value))throw std::runtime_error("Non-finite saved state");
        const auto* first=reinterpret_cast<const uint8_t*>(&value);
        bytes.insert(bytes.end(),first,first+sizeof(T));
    }
    void flag(bool value){put<uint8_t>(value?1:0);}
    void text(const std::string& value) {
        if(value.size()>4096)throw std::runtime_error("Saved string is too large");
        put<uint32_t>(uint32_t(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());
    }
    void blob(const Bytes& value) {
        if(value.size()>16*1024*1024)throw std::runtime_error("Saved script state is too large");
        put<uint32_t>(uint32_t(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());
    }
    void flags(const std::vector<bool>& values) {
        put<uint32_t>(uint32_t(values.size()));for(bool value:values)flag(value);
    }
};
struct Reader {
    const Bytes& bytes;size_t cursor=0;
    template<class T> T get() {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>);
        if(cursor>bytes.size()||sizeof(T)>bytes.size()-cursor)throw std::runtime_error("Truncated saved state");
        T value;std::memcpy(&value,bytes.data()+cursor,sizeof(value));cursor+=sizeof(value);
        if constexpr(std::is_floating_point_v<T>)
            if(!std::isfinite(value))throw std::runtime_error("Non-finite saved state");
        return value;
    }
    uint32_t count(uint32_t maximum) {
        const auto value=get<uint32_t>();if(value>maximum)throw std::runtime_error("Excessive saved state count");return value;
    }
    bool flag(){const auto value=get<uint8_t>();if(value>1)throw std::runtime_error("Invalid saved boolean");return value==1;}
    std::string text() {
        const auto length=count(4096);
        if(cursor>bytes.size()||length>bytes.size()-cursor)throw std::runtime_error("Truncated saved string");
        std::string result(reinterpret_cast<const char*>(bytes.data()+cursor),length);cursor+=length;return result;
    }
    Bytes blob() {
        const auto length=count(16*1024*1024);
        if(cursor>bytes.size()||length>bytes.size()-cursor)throw std::runtime_error("Truncated saved script state");
        Bytes result(bytes.begin()+cursor,bytes.begin()+cursor+length);cursor+=length;return result;
    }
    std::vector<bool> flags(uint32_t expected) {
        if(count(expected)!=expected)throw std::runtime_error("Saved geometry count does not match scene");
        std::vector<bool> result;result.reserve(expected);for(uint32_t i=0;i<expected;++i)result.push_back(flag());return result;
    }
    void end()const{if(cursor!=bytes.size())throw std::runtime_error("Trailing saved state data");}
};
static_assert(sizeof(float)==4&&sizeof(double)==8,"Saved state requires IEEE-sized scalars");
}
