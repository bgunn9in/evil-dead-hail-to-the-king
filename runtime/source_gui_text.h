#pragma once
#include "source_gui.h"
#include "source_font.h"
#include "object_mesh.h"
#include "ui_language.h"
#include <functional>
#include <map>
#include <optional>

namespace edguitext {
struct Bounds {float left=0,bottom=0,right=0,top=0;};
struct Rect {int left=0,top=0,right=0,bottom=0;};
struct Glyph {int x=0,y=0;uint8_t code=0;uint32_t color=0xffffffff;int w=14,h=22;}; // D3DCOLOR ARGB.
struct Page {std::vector<Glyph> glyphs;uint32_t nextOffset=0;Rect bounds;bool background=false,clip=false;};
struct GlyphCrop {int x=0,y=0,w=0,h=0;float u0=0,v0=0,u1=1,v1=1;};
inline GlyphCrop croppedGlyph(const Page& page,const Glyph& glyph) {
    GlyphCrop out{glyph.x,glyph.y,glyph.w,glyph.h};
    if(glyph.w<=0||glyph.h<=0)throw jpc::Error("Invalid GUI glyph dimensions");
    if(!page.clip)return out;
    const int l=std::max(glyph.x,page.bounds.left),t=std::max(glyph.y,page.bounds.top),
        r=std::min(glyph.x+glyph.w,page.bounds.right),b=std::min(glyph.y+glyph.h,page.bounds.bottom);
    if(l>=r||t>=b)return {};
    return {l,t,r-l,b-t,float(l-glyph.x)/glyph.w,float(t-glyph.y)/glyph.h,
        float(r-glyph.x)/glyph.w,float(b-glyph.y)/glyph.h};
}
class Catalog {
    std::map<std::pair<uint32_t,uint32_t>,std::string> strings;
    std::map<uint32_t,std::vector<Bounds>> models;
    std::map<std::pair<uint32_t,uint32_t>,std::string> translated;
    uint64_t textRevision_=0;
    UiLanguage language_=UiLanguage::Russian;
public:
    explicit Catalog(const std::filesystem::path& root) {
        const auto text=edobjects::detail::read(root/"gui/text.bin",8*1024*1024);edobjects::detail::Reader input{text};input.magic("EGTEXT2\0");
        const auto count=input.get<uint32_t>();if(count>10000)throw jpc::Error("Excessive source GUI text catalog");
        for(unsigned n=0;n<count;++n) {
            const auto scope=input.get<uint32_t>(),id=input.get<uint32_t>(),size=input.get<uint32_t>();
            if(!id||!size||size>32767||size>text.size()-std::min(input.cursor,text.size()))throw jpc::Error("Invalid source GUI text record");
            std::string value;for(unsigned k=0;k<size;++k){const auto c=input.get<uint8_t>();
                if(k+1==size){if(c)throw jpc::Error("Unterminated source GUI text");}
                else {if(!c||c>126||(c<32&&c!=9&&c!=10&&c!=13))throw jpc::Error("Invalid source GUI glyph");value.push_back(char(c));}}
            if(!strings.emplace(std::make_pair(scope,id),std::move(value)).second)throw jpc::Error("Duplicate scoped source GUI text");
        }
        if(input.cursor!=text.size())throw jpc::Error("Trailing source GUI text bytes");
        if(std::filesystem::is_regular_file(root/"gui/text-ru.bin")){
            const auto bytes=edobjects::detail::read(root/"gui/text-ru.bin",8*1024*1024);edobjects::detail::Reader locale{bytes};locale.magic("EGTEXT3\0");
            if(locale.get<uint32_t>()!=1251)throw jpc::Error("Unsupported declared GUI text encoding");
            const auto count=locale.get<uint32_t>();if(count!=strings.size())throw jpc::Error("Localized GUI catalog key count differs");
            for(unsigned n=0;n<count;++n){const auto scope=locale.get<uint32_t>(),id=locale.get<uint32_t>(),size=locale.get<uint32_t>();
                if(!size||size>32767||size>bytes.size()-std::min(locale.cursor,bytes.size())||!strings.count({scope,id}))throw jpc::Error("Invalid localized GUI text record");
                std::string value;for(unsigned k=0;k<size;++k){const auto code=locale.get<uint8_t>();if(k+1==size){if(code)throw jpc::Error("Unterminated localized GUI text");}
                    else {if(!edfont::cp1251Byte(code)&&code!=9&&code!=10&&code!=13)throw jpc::Error("Invalid CP1251 GUI text byte");value.push_back(char(code));}}
                if(!translated.emplace(std::make_pair(scope,id),std::move(value)).second)throw jpc::Error("Duplicate localized GUI text");}
            if(locale.cursor!=bytes.size())throw jpc::Error("Trailing localized GUI text bytes");
            if(!translated.empty()){textRevision_=14695981039346656037ull;
                for(uint8_t code:bytes){textRevision_^=code;textRevision_*=1099511628211ull;}
                if(!textRevision_)textRevision_=1;}
        }

        const auto geometry=edobjects::detail::read(root/"gui/layout.bin",1024*1024);edobjects::detail::Reader layout{geometry};layout.magic("EGLAY1\0\0");
        const auto meshes=layout.get<uint32_t>();if(meshes>256)throw jpc::Error("Excessive GUI layout catalog");
        for(unsigned n=0;n<meshes;++n){const auto id=layout.get<uint32_t>(),parts=layout.get<uint32_t>();
            if(!id||!parts||parts>256||models.count(id))throw jpc::Error("Invalid GUI layout model");
            auto& target=models[id];for(unsigned part=0;part<parts;++part){Bounds b{layout.get<float>(),layout.get<float>(),layout.get<float>(),layout.get<float>()};
                if(!std::isfinite(b.left)||!std::isfinite(b.right)||!std::isfinite(b.bottom)||!std::isfinite(b.top)||
                    b.left>b.right||b.bottom>b.top||std::max({std::abs(b.left),std::abs(b.right),std::abs(b.bottom),std::abs(b.top)})>1000)
                    throw jpc::Error("Invalid authored GUI bounds");target.push_back(b);}}
        if(layout.cursor!=geometry.size())throw jpc::Error("Trailing source GUI layout bytes");
    }
    const std::string& text(uint32_t scene,uint32_t id)const {
        return textForLanguage(scene,id,language_);
    }
    const std::string& textForLanguage(uint32_t scene,uint32_t id,UiLanguage language)const {
        if(!validUiLanguage(language))throw jpc::Error("Unsupported GUI language");
        const auto& catalog=russianUi(language)&&!translated.empty()?translated:strings;
        auto found=catalog.find({scene,id});if(found==catalog.end())found=catalog.find({0,id});
        if(found==catalog.end())throw jpc::Error("Original scoped GUI text is unavailable: "+std::to_string(id));return found->second;
    }
    const std::string& lookup(uint32_t scene,uint32_t id)const {return text(scene,id);}
    void setLanguage(UiLanguage value){if(!validUiLanguage(value))throw jpc::Error("Unsupported GUI language");language_=value;}
    UiLanguage language()const{return language_;}
    bool localized()const{return russianUi(language_)&&!translated.empty();}
    uint64_t textRevision()const{return localized()?textRevision_:0;}
    bool contains(uint32_t scene,uint32_t id)const{return strings.count({scene,id})||strings.count({0,id});}
    const std::string& originalText(uint32_t scene,uint32_t id)const{auto found=strings.find({scene,id});if(found==strings.end())found=strings.find({0,id});if(found==strings.end())throw jpc::Error("Original GUI text missing");return found->second;}
    const Bounds& bounds(uint32_t model,unsigned part)const {
        const auto found=models.find(model);if(found==models.end()||part>=found->second.size())throw jpc::Error("Original GUI Maya part is unavailable");return found->second[part];
    }
    void validate(const SourceGui& gui,uint32_t scene,const std::function<std::optional<std::string>(uint32_t)>& special={},int forcedItem=-1)const {
        if(forcedItem< -1||forcedItem>=int(gui.items.size()))throw jpc::Error("Saved forced GUI reader item is out of range");
        for(size_t index=0;index<gui.items.size();++index) {
            const auto& item=gui.items[index];const bool forced=int(index)==forcedItem;
            if(item.textOffset<0)throw jpc::Error("Saved GUI cursor is negative");
            if(!item.textOffset&&!forced)continue;
            const bool visible=(item.flags&9u)==9u&&item.owner>=0&&item.owner<int(gui.systems.size())&&
                (gui.systems[size_t(item.owner)].flags&9u)==9u;
            //41FDD0 never resolves hidden items, including stale text retained
            // after a scene unload. A modern document panel is explicit.
            if(!visible&&!forced)continue;
            if(forced&&!(item.flags&8u))throw jpc::Error("Saved forced GUI reader is unallocated");
            const auto renderer=item.properties.find("guiItemSetRenderFunc"),id=item.properties.find("guiItemSetDataID");
            if(item.textOffset<0||renderer==item.properties.end()||renderer->second.size()!=1||renderer->second[0]!=3||
                id==item.properties.end()||id->second.size()!=1)throw jpc::Error("Saved GUI cursor has no source text item");
            const uint32_t resource=uint32_t(id->second[0]);std::optional<std::string> alternate;
            if(special)alternate=special(resource);
            const auto& raw=alternate?*alternate:text(scene,resource);
            if(size_t(item.textOffset)>raw.size())throw jpc::Error("Saved GUI cursor exceeds original text");
        }
    }
};
inline void restartTextCursors(SourceGui& gui,jpc::Interpreter& vm) {
    for(unsigned index=0;index<gui.items.size();++index){auto& item=gui.items[index];
        const auto renderer=item.properties.find("guiItemSetRenderFunc"),data=item.properties.find("guiItemSetDataID");
        if(!(item.flags&8u)||renderer==item.properties.end()||renderer->second!=std::vector<int32_t>{3}||
           data==item.properties.end()||data->second.size()!=1)continue;
        const int id=data->second[0];gui.native(vm,"guiItemSetDataID",{jpc::Value::integer(int(index)),jpc::Value::integer(id)});
        item.flags&=~0x200u; // A pending advance belongs to the previous text layout.
    }
}
inline Rect window(const Bounds& b,const SourceGui::Record& item,const SourceGui::Record& system) {
    const auto word=[](int value){return int(int16_t(uint16_t(value)));};
    const int left=word(320-int(-20.f*b.left)),right=word(320-int(-20.f*b.right)),
        top=word(240-int(20.f*b.top)),bottom=word(240-int(20.f*b.bottom));
    const int x=2*word(left/2+(int(item.x)+system.x)/2),y=2*word(top/2+(int(item.y)+system.y)/2);
    return {x,y,x+2*(right/2-left/2),y+2*(bottom/2-top/2)};
}
namespace detail {
struct State {uint32_t color=0xffffffff,base=0xffffffff;};
inline bool delimiter(char c){return c==' '||c=='\t'||c=='\n'||c=='@';}
struct Word {size_t end=0;unsigned width=0;};
inline Word word(const std::string& text,size_t start,State& state,std::vector<Glyph>* draw,int x,int y,unsigned limit=~0u) {
    size_t at=start;unsigned width=0;
    while(at<text.size()&&!delimiter(text[at])) {
        if(text[at]=='$'&&at+1<text.size()) {
            const char command=text[at+1];at+=2;
            if(command=='A') {if(text.size()-at<9)throw jpc::Error("Truncated original text color");uint32_t rgb=0;
                for(unsigned channel=0;channel<3;++channel){unsigned value=0;for(unsigned digit=0;digit<3;++digit){const char c=text[at++];
                    if(c<'0'||c>'9')throw jpc::Error("Invalid original text color");value=value*10+unsigned(c-'0');}rgb=(rgb<<8)|value;}
                state.color=0xff000000u|rgb;continue;}
            if(command=='C'){state.color=state.base;continue;}
            if(command=='B')throw jpc::Error("Original alternate GUI font is not exported");
            // Native409BD1 consumes an unknown two-byte control prefix too.
            if(at==text.size())break;
        }
        if(at==text.size()||delimiter(text[at]))break;
        if(width>limit||limit-width<14)break;
        const auto code=uint8_t(text[at++]);if(draw)draw->push_back({x+int(width),y,code=='^'?uint8_t('\''):code,state.color});width+=14;
    }
    return {at,width};
}
}
inline Page layout(const std::string& text,uint32_t offset,Rect outer,unsigned alignment=0,uint32_t color=0xffffffff,bool background=false,bool splitLongWords=false) {
    if(text.size()>32766||offset>text.size()||outer.left>outer.right||outer.top>outer.bottom||alignment>2)
        throw jpc::Error("Invalid bounded original text layout");
    Page result;result.bounds=outer;result.background=background;Rect area=outer;
    if(background){area.left+=8;area.top+=8;area.right-=8;area.bottom-=8;}
    detail::State state{color,color};size_t line=offset;int y=area.top;
    for(size_t lines=0;lines<=text.size()+1;++lines) {
        if(line==text.size()){result.nextOffset=0;return result;}
        size_t scan=line,accepted=line;unsigned width=0;
        while(scan<text.size()) {
            if(text[scan]=='\n'||text[scan]=='@'){accepted=++scan;break;}
            unsigned space=0;while(scan<text.size()&&(text[scan]==' '||text[scan]=='\t')){space+=text[scan]==' '?14:112;++scan;}
            if(scan==text.size())break;
            if(text[scan]=='\n'||text[scan]=='@')continue;
            auto measure=state;const auto word=detail::word(text,scan,measure,nullptr,0,0);
            if(word.end==scan)throw jpc::Error("Original text parser made no progress");
            const unsigned candidate=width+(width?space:0)+word.width;
            if(int64_t(y)+22>area.bottom)break;
            if(int64_t(candidate)>int64_t(area.right)-area.left){
                if(!width&&splitLongWords&&area.right-area.left>=14){
                    auto fragment=state;const auto partial=detail::word(text,scan,fragment,nullptr,0,0,unsigned(area.right-area.left));
                    width=partial.width;accepted=partial.end;
                }
                break;
            }
            width=candidate;accepted=scan=word.end;
        }
        if(accepted==line){result.nextOffset=uint32_t(line);return result;}
        int x=area.left;if(alignment==1)x+=area.right-area.left-int(width);else if(alignment==2)x+=(area.right-area.left-int(width))/2;
        scan=line;unsigned drawn=0;
        while(scan<accepted) {
            if(text[scan]=='\n'||text[scan]=='@'){++scan;break;}
            unsigned space=0;while(scan<accepted&&(text[scan]==' '||text[scan]=='\t')){space+=text[scan]==' '?14:112;++scan;}
            if(scan>=accepted||text[scan]=='\n'||text[scan]=='@')continue;
            if(drawn)x+=int(space);
            const auto word=detail::word(text,scan,state,&result.glyphs,x,y,width-drawn);x+=int(word.width);drawn+=word.width;scan=word.end;
        }
        y+=22;line=accepted;
    }
    throw jpc::Error("Original text line bound exceeded");
}
// Presentation-only motion: the complete source string and VM page cursor stay intact.
inline Page marquee(const std::string& text,Rect area,unsigned alignment,uint32_t color,double seconds=0) {
    if(!std::isfinite(seconds)||seconds<0)throw jpc::Error("Invalid GUI presentation clock");
    auto page=layout(text,0,{0,area.top,500000,area.top+22},0,color);
    int width=0;for(const auto& glyph:page.glyphs)width=std::max(width,glyph.x+glyph.w);
    const int available=area.right-area.left,travel=std::max(0,width-available);
    int shift=0;
    if(travel){const double duration=double(travel)/30.0,phase=std::fmod(seconds,2*duration+2);
        shift=int(std::lround(phase<1?0:phase<duration+1?(phase-1)*30:phase<duration+2?travel:(2*duration+2-phase)*30));}
    const int x=area.left+(travel? -shift:alignment==1?available-width:alignment==2?(available-width)/2:0);
    for(auto& glyph:page.glyphs)glyph.x+=x;
    page.bounds=area;page.clip=true;page.nextOffset=0;return page;
}
inline Page pagedNotice(const std::string& text,Rect area,uint32_t color,double seconds=0) {
    if(!std::isfinite(seconds)||seconds<0)throw jpc::Error("Invalid GUI presentation clock");
    std::vector<Page> pages;uint32_t offset=0;
    do{auto page=layout(text,offset,area,2,color,false,true);
        if(page.nextOffset&&page.nextOffset<=offset)throw jpc::Error("GUI viewport cannot fit one glyph");
        offset=page.nextOffset;page.clip=true;pages.push_back(std::move(page));
        if(pages.size()>4096)throw jpc::Error("Excessive GUI notice pages");
    }while(offset);
    return std::move(pages[size_t(std::fmod(std::floor(seconds/4),double(pages.size())))]);
}
struct Box {int item=-1;uint32_t asset=0;Page page;};
inline void commitAdvance(SourceGui::Record& item,const Page& page) {
    if(page.nextOffset>32767)throw jpc::Error("Original GUI next-page cursor exceeds signed16");
    if(item.flags&0x200u){item.flags^=0x200u;item.textOffset=int16_t(uint16_t(page.nextOffset));}
}
inline Box sample(SourceGui& gui,const Catalog& assets,uint32_t scene,int itemIndex,int mode,bool forceVisible=false,const std::string* resolvedText=nullptr,double seconds=0) {
    auto& item=SourceGui::at(gui.items,itemIndex);const auto& system=SourceGui::at(gui.systems,item.owner);
    Box result;result.item=itemIndex;
    if(!forceVisible&&(!(system.flags&1)||!(item.flags&1)))return result;
    auto property=[&](const char* name,size_t argc)->const std::vector<int32_t>&{const auto found=item.properties.find(name);
        if(found==item.properties.end()||found->second.size()!=argc)throw jpc::Error("Original text item property is missing");return found->second;};
    if(property("guiItemSetRenderFunc",1)[0]!=3)throw jpc::Error("Source GUI item is not a text renderer");
    const auto& maya=property("guiItemSetMayaInfo",3);result.asset=uint32_t(property("guiItemSetDataID",1)[0]);
    if(!result.asset||result.asset==0xffffffffu)return result;
    auto rect=window(assets.bounds(uint32_t(maya[0]),unsigned(uint8_t(maya[2]))),item,system);
    const bool background=mode==0||(mode==3&&((itemIndex>=0x21&&itemIndex<=0x25)||(itemIndex>=0x30&&itemIndex<=0x34)));
    if(background&&(itemIndex==0x32||itemIndex==0x35)){rect.top+=2;rect.bottom+=2;}
    const unsigned alignment=item.flags&0x4000?0:item.flags&0x8000?1:item.flags&0x10000?2:0;
    uint32_t color=0xffffffffu;
    const auto tint=item.properties.find("guiItemSetColor");
    if(tint!=item.properties.end()) {
        if(tint->second.size()!=4)throw jpc::Error("Original text color has an invalid argument count");
        uint32_t rgba=0;for(int32_t channel:tint->second)rgba=(rgba<<8)|uint32_t(channel);
        //40BAA0 packs RGBA;41FF16 discards alpha and halves disabled RGB.
        color=0xff000000u|((item.flags&0x10u)?rgba>>8:(rgba>>9)&0x7f7f7fu);
    }
    const auto& raw=resolvedText?*resolvedText:assets.text(scene,result.asset);
    if(!background&&rect.bottom-rect.top<44&&raw.find_first_of("@\n")==std::string::npos&&!item.textOffset)
        result.page=marquee(raw,rect,alignment,color,seconds);
    else result.page=layout(raw,uint16_t(item.textOffset),rect,alignment,color,background,true);
    commitAdvance(item,result.page);
    return result;
}
}
