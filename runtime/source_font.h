#pragma once
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
namespace edfont {
inline bool cp1251Byte(uint8_t code){return code>=32&&code!=127&&code!=152;}
inline size_t glyph(const std::string& charset,uint8_t code,bool last=true){
    auto find=[&](uint8_t c){return last?charset.rfind(char(c)):charset.find(char(c));};
    auto index=find(code);if(index==std::string::npos&&code==39)index=find('^');return index;
}
struct Metadata {
    unsigned width=0,height=0,columns=0,atlasWidth=256,atlasHeight=256,codepage=0,atlasScale=1;
    std::string charset;
    static Metadata read(std::istream& input){Metadata result;std::string magic,encoding;
        input>>magic>>result.width>>result.height>>result.columns;
        if(magic=="EDFONT2"){input>>result.atlasWidth>>result.atlasHeight;
            input.ignore(std::numeric_limits<std::streamsize>::max(),'\n');std::getline(input,encoding);if(!encoding.empty()&&encoding.back()=='\r')encoding.pop_back();
            if(encoding!="CP1251")throw std::runtime_error("Unsupported declared font encoding");result.codepage=1251;
        }else if(magic=="EDFONT1")input.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        else throw std::runtime_error("Unsupported font atlas metadata");
        std::getline(input,result.charset);if(!result.charset.empty()&&result.charset.back()=='\r')result.charset.pop_back();
        result.atlasScale=result.atlasWidth==512?2:1;
        if(!input||!result.width||!result.height||!result.columns||result.width>256||result.height>256||
           (result.atlasWidth!=256&&result.atlasWidth!=512)||(result.atlasHeight!=256*result.atlasScale&&result.atlasHeight!=512*result.atlasScale)||
           result.width*uint64_t(result.columns)>result.atlasWidth/result.atlasScale||result.charset.empty()||result.charset.size()>256||
           ((result.charset.size()+result.columns-1)/result.columns)*result.height>result.atlasHeight/result.atlasScale)
            throw std::runtime_error("Invalid bounded font atlas metadata");
        for(uint8_t code:result.charset)if(result.codepage==1251?!cp1251Byte(code):(code<32||code>126))throw std::runtime_error("Invalid declared font glyph code");
        return result;
    }
};
}
