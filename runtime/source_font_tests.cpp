#include "source_font.h"
#include "source_gui_text.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>

namespace fs=std::filesystem;
using Bytes=std::vector<uint8_t>;
static void put(Bytes& bytes,uint32_t word){for(unsigned n=0;n<4;++n)bytes.push_back(uint8_t(word>>(8*n)));}
static void write(const fs::path& path,const Bytes& bytes){std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!output)throw std::runtime_error("Cannot write owned font fixture");}
static Bytes catalog(bool localized,const std::vector<std::tuple<uint32_t,uint32_t,std::string>>& rows,uint32_t codepage=1251){
    const std::string magic=localized?std::string("EGTEXT3\0",8):std::string("EGTEXT2\0",8);Bytes bytes(magic.begin(),magic.end());
    if(localized)put(bytes,codepage);put(bytes,uint32_t(rows.size()));
    for(const auto& [scope,id,text]:rows){put(bytes,scope);put(bytes,id);put(bytes,uint32_t(text.size()+1));bytes.insert(bytes.end(),text.begin(),text.end());bytes.push_back(0);}return bytes;
}
template<class F> static void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){try{
    std::istringstream legacy("EDFONT1\n18 24 14\nAA^?\n");const auto old=edfont::Metadata::read(legacy);
    assert(old.codepage==0&&old.atlasWidth==256&&old.atlasHeight==256);
    assert(edfont::glyph(old.charset,'A')==1&&edfont::glyph(old.charset,'A',false)==0&&edfont::glyph(old.charset,39)==2);
    const std::string charset="A^?\xcf\xf0\xe8\xe2\xe5\xf2";
    std::istringstream modern("EDFONT2\n18 24 14 256 512\nCP1251\n"+charset+"\n");const auto font=edfont::Metadata::read(modern);
    assert(font.codepage==1251&&font.atlasHeight==512&&edfont::glyph(font.charset,0xcf)==3&&edfont::glyph(font.charset,39)==1);
    std::istringstream enlarged("EDFONT2\n18 24 14 512 1024\nCP1251\n"+charset+"\n");const auto doubled=edfont::Metadata::read(enlarged);
    assert(doubled.atlasScale==2&&doubled.width==font.width&&doubled.height==font.height&&doubled.columns==font.columns&&doubled.charset==font.charset);
    assert(doubled.atlasWidth/doubled.atlasScale==font.atlasWidth&&doubled.atlasHeight/doubled.atlasScale==font.atlasHeight);
    for(const std::string& text:std::vector<std::string>{"EDFONT2\n18 24 14 256 512\nUTF8\nABC\n","EDFONT2\n18 24 0 256 512\nCP1251\nABC\n",
            "EDFONT2\n256 24 2 256 512\nCP1251\nABC\n","EDFONT2\n18 24 14 512 256\nCP1251\nABC\n",
            "EDFONT2\n18 24 14 512 768\nCP1251\nABC\n","EDFONT2\n18 24 14 1024 2048\nCP1251\nABC\n",
            "EDFONT2\n36 48 14 512 1024\nCP1251\nABC\n",std::string("EDFONT2\n18 24 14 256 512\nCP1251\n")+char(152)+"\n"})
        rejects([&]{std::istringstream input(text);edfont::Metadata::read(input);});
    const auto root=fs::temp_directory_path()/("evil-dead-font-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(!fs::exists(root));fs::create_directories(root/"gui");
    Bytes geometry{'E','G','L','A','Y','1',0,0};put(geometry,0);write(root/"gui/layout.bin",geometry);
    const auto english=catalog(false,{{0,1,"English"},{123,1,"Scoped"}});write(root/"gui/text.bin",english);
    {edguitext::Catalog assets(root);assert(!assets.localized()&&assets.textRevision()==0&&assets.lookup(0,1)=="English"&&assets.lookup(123,1)=="Scoped");}
    const std::string russian="\xcf\xf0\xe8\xe2\xe5\xf2";
    const auto localized=catalog(true,{{0,1,russian},{123,1,russian+"!"}});write(root/"gui/text-ru.bin",localized);
    {edguitext::Catalog assets(root);assert(assets.localized()&&assets.lookup(456,1)==russian&&assets.lookup(123,1)==russian+"!"&&assets.originalText(123,1)=="Scoped");
        const auto revision=assets.textRevision();assert(revision&&edguitext::Catalog(root).textRevision()==revision);
        write(root/"gui/text-ru.bin",catalog(true,{{0,1,russian+"!"},{123,1,russian+"!"}}));
        assert(edguitext::Catalog(root).textRevision()!=revision);write(root/"gui/text-ru.bin",localized);
        const auto first=edguitext::layout(russian+" "+russian,0,{0,0,84,22});assert(first.glyphs.size()==6&&first.glyphs.front().code==0xcf&&first.nextOffset==6);
        const auto second=edguitext::layout(russian+" "+russian,first.nextOffset,{0,0,84,22});assert(second.glyphs.size()==6&&second.nextOffset==0);
        SourceGui::Record item;item.textOffset=6;item.flags=0x200;edguitext::commitAdvance(item,second);assert(item.textOffset==0&&!(item.flags&0x200));
        const auto colored=edguitext::layout("$A032048128"+russian+"$C ^",0,{0,0,140,22});assert(colored.glyphs.size()==7&&colored.glyphs.front().color==0xff203080&&colored.glyphs.back().code==39&&colored.glyphs.back().color==0xffffffff);}
    for(auto bytes:{catalog(true,{{0,1,russian},{123,1,russian}},65001),catalog(true,{{0,1,russian}}),
            catalog(true,{{0,1,russian},{0,1,russian}}),catalog(true,{{0,1,russian},{456,1,russian}}),
            catalog(true,{{0,1,std::string(1,char(152))},{123,1,russian}})}){
        write(root/"gui/text-ru.bin",bytes);rejects([&]{edguitext::Catalog invalid(root);});}
    auto broken=localized;broken.back()='x';write(root/"gui/text-ru.bin",broken);rejects([&]{edguitext::Catalog invalid(root);});
    broken=localized;broken.push_back(0);write(root/"gui/text-ru.bin",broken);rejects([&]{edguitext::Catalog invalid(root);});
    fs::remove(root/"gui/text-ru.bin");write(root/"gui/text.bin",catalog(false,{{0,1,russian}}));rejects([&]{edguitext::Catalog invalid(root);});
    for(const char* name:{"text.bin","layout.bin"})fs::remove(root/"gui"/name);fs::remove(root/"gui");fs::remove(root);
    std::cout<<"Declared font/CP1251 catalog: legacy aliases, scoped keys, byte paging/color, corrupt/codepage/keyset/ASCII negatives PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
