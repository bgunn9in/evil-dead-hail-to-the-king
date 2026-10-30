#pragma once
#include "jpc.h"
#include <SDL3/SDL_events.h>
#include <algorithm>
#include <string>
#include <vector>
#include "ui_strings.h"
#include "utility_menu.h"
#include "source_font.h"

struct DebugSettings {
    bool invulnerable=false,infiniteFuel=false,infiniteAmmo=false;
};
namespace eddebug {
enum class Category:unsigned {All,Weapons,Ammo,Health,Fuel,Materials,Quest,Documents,Save,Count};
inline const char* categoryName(Category value){
    static const char* names[]={"all items","weapons","ammunition","health","fuel","materials","quest items","documents","saving"};
    if(value>=Category::Count)throw std::out_of_range("Unknown debug item category");return names[unsigned(value)];
}
}
struct DebugItem {
    int id=0;std::string name;eddebug::Category category=eddebug::Category::Quest;
    std::string english,russian;
    const std::string& label(bool ru)const {const auto& value=ru?russian:english;return value.empty()?name:value;}
};
struct DebugMenu {
    DebugSettings settings;
    bool open=false;
    unsigned selected=0,item=0;
    int quantity=1;
    std::vector<DebugItem> items;
    std::string notice;
    static constexpr unsigned PageSize=8;
    bool selecting=false;
    eddebug::Category category=eddebug::Category::All;
    std::string query;
    unsigned choice=0;
    static std::string folded(std::string value){for(char& c:value){const auto b=uint8_t(c);
        if(b>='A'&&b<='Z')c=char(b+32);else if(b>=0xc0&&b<=0xdf)c=char(b+32);else if(b==0xa8)c=char(0xb8);}return value;}
    std::vector<unsigned> matches()const {
        const auto key=folded(query);std::vector<unsigned> result;
        for(unsigned n=0;n<items.size();++n){const auto& entry=items[n];
            if(category!=eddebug::Category::All&&entry.category!=category)continue;
            if(key.empty()||folded(entry.name+" "+entry.english+" "+entry.russian+" "+std::to_string(entry.id)).find(key)!=std::string::npos)result.push_back(n);}
        return result;
    }
    void beginSelection(){selecting=true;query.clear();category=eddebug::Category::All;choice=item<items.size()?item:0;notice.clear();}
    bool confirmSelection(){const auto found=matches();if(!selecting||found.empty())return false;
        item=found[std::min(choice,unsigned(found.size()-1))];selecting=false;notice.clear();return true;}
    bool wantsTextInput()const{return open&&selecting;}
    void back(){if(selecting){selecting=false;query.clear();}else open=false;}
    void changeCategory(int delta){const int count=int(eddebug::Category::Count);
        category=eddebug::Category((int(category)+delta%count+count)%count);choice=0;}
    void page(int delta){const auto count=unsigned(matches().size());if(count)choice=unsigned(std::clamp(int(choice)+delta*int(PageSize),0,int(count)-1));}
    bool onEvent(const SDL_Event& event){
        if(!wantsTextInput())return false;
        if(event.type==SDL_EVENT_TEXT_INPUT){
            std::string add;const auto* p=reinterpret_cast<const unsigned char*>(event.text.text);
            if(!p)return true;
            for(size_t n=0;p[n];){unsigned code=p[n++];
                if(code>=0xc2&&code<=0xdf){const unsigned low=p[n];if(low<0x80||low>0xbf)return true;++n;code=((code&31)<<6)|(low&63);}
                else if(code>=0x80)return true;
                if(code>=0x410&&code<=0x44f)code=code-0x410+0xc0;else if(code==0x401)code=0xa8;else if(code==0x451)code=0xb8;
                else if(code<32||code>126||code=='$'||code=='@')return true;
                add.push_back(char(code));}
            if(query.size()+add.size()<=64){query+=add;choice=0;}return true;
        }
        if(event.type==SDL_EVENT_KEY_DOWN){const auto code=event.key.scancode;
            if(code==SDL_SCANCODE_BACKSPACE){if(!query.empty())query.pop_back();choice=0;return true;}
            if(code==SDL_SCANCODE_PAGEUP||code==SDL_SCANCODE_PAGEDOWN){page(code==SDL_SCANCODE_PAGEUP?-1:1);return true;}
            if(code==SDL_SCANCODE_TAB&&!event.key.repeat){changeCategory((event.key.mod&SDL_KMOD_SHIFT)?-1:1);return true;}
            // Text entry must not turn E into Main's confirm shortcut or WASD into navigation.
            if(code!=SDL_SCANCODE_ESCAPE&&code!=SDL_SCANCODE_RETURN&&code!=SDL_SCANCODE_KP_ENTER&&code!=SDL_SCANCODE_PAUSE&&
               code!=SDL_SCANCODE_UP&&code!=SDL_SCANCODE_DOWN&&code!=SDL_SCANCODE_LEFT&&code!=SDL_SCANCODE_RIGHT)return true;}
        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){if(event.gbutton.button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER||event.gbutton.button==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER){
            changeCategory(event.gbutton.button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER?-1:1);return true;}}
        return false;
    }
    static bool toggleKey(const SDL_Event& event){return event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_PAUSE;}
    void toggle(){open=!open;selected=0;selecting=false;query.clear();notice.clear();}
    void move(int direction) {
        if(selecting){if(direction==1||direction==-1){page(direction);return;}
            const auto count=unsigned(matches().size());if(count&&(direction==4||direction==-4))choice=unsigned((int(choice)+(direction>0?1:int(count)-1))%int(count));return;}
        if(direction==4||direction==-4)selected=unsigned((int(selected)+(direction>0?1:5))%6);
        else if(direction==1||direction==-1) {
            if(selected==0&&!items.empty())item=unsigned((int(item)+(direction>0?1:int(items.size())-1))%int(items.size()));
            if(selected==1)quantity=std::clamp(quantity+direction,1,999);
        }
    }
    edutility::Menu presentation(bool russian=false)const {
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        const auto on=[&](bool value){return t(value?"on":"off");};
        if(selecting){const auto found=matches();const auto focus=found.empty()?0:std::min(choice,unsigned(found.size()-1));
            edutility::Menu menu;menu.title=t("choose item")+": "+t(eddebug::categoryName(category));
            const unsigned begin=focus/PageSize*PageSize,end=std::min(unsigned(found.size()),begin+PageSize);
            menu.focus=focus-begin;
            for(unsigned n=begin;n<end;++n){const auto& entry=items[found[n]];
                menu.rows.push_back({std::to_string(entry.id)+" "+entry.label(russian),n==focus});}
            menu.details={{t("search: ")+query,false},{t("page ")+std::to_string(focus/PageSize+1)+"/"+std::to_string(std::max(1u,(unsigned(found.size())+PageSize-1)/PageSize)),false}};
            if(found.empty())menu.rows={{t("no matching items"),false}};
            menu.footer={t("lb/rb/tab: category  left/right: page"),t("type: search  a: give  b: back")};return menu;}
        const std::string object=items.empty()?t("no valid items"):
            std::to_string(items.at(item).id)+" "+items.at(item).label(russian);
        const std::string rows[]={t("give: ")+object,t("quantity: ")+std::to_string(quantity),
            t("invulnerability: ")+on(settings.invulnerable),t("infinite fuel: ")+on(settings.infiniteFuel),
            t("infinite ammo: ")+on(settings.infiniteAmmo),t("close")};
        edutility::Menu menu;menu.title=t("debug menu");menu.focus=selected;
        for(unsigned n=0;n<6;++n)menu.rows.push_back({rows[n],selected==n});
        menu.footer={t("up/down: row  left/right: change"),t("enter/e: apply  pause/esc: close")};
        menu.notice=t(notice);
        for(char& c:menu.notice)if(!edfont::cp1251Byte(uint8_t(c))||c=='$'||c=='@')c=' ';
        return menu;
    }
    std::string text(bool russian=false)const {
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        const auto on=[&](bool value){return t(value?"on":"off");};
        const std::string object=items.empty()?t("no valid items"):std::to_string(items[item].id)+" "+items[item].name.substr(0,18);
        const std::string rows[]={t("give: ")+object,t("quantity: ")+std::to_string(quantity),
            t("invulnerability: ")+on(settings.invulnerable),t("infinite fuel: ")+on(settings.infiniteFuel),
            t("infinite ammo: ")+on(settings.infiniteAmmo),t("close")};
        std::string result=t("debug menu")+'\n';
        for(unsigned n=0;n<6;++n)result+=(selected==n?"> ":"  ")+rows[n]+"\n";
        return result+t("up/down: row  left/right: change")+'\n'+t("enter/e: apply  pause/esc: close")+'\n'+t(notice).substr(0,32);
    }
};

namespace eddebug {
inline std::vector<int> sourceLookupIDs(const jpc::Package& package,const char* name) {
    const auto& method=package.methods.at(size_t(package.methodIndex("ObjDB",name,"(I)I")));
    if(!method.hasCode||method.length<12)throw jpc::Error("Debug item catalog has no source lookup");
    const size_t begin=56+method.offset,end=begin+method.length;
    if(end>package.data.size())throw jpc::Error("Debug item lookup exceeds source package");
    // Native flags use iload0/switch; health/fuel first initialize local1 to zero.
    const bool direct=package.data.at(begin)==0x1a&&package.data.at(begin+1)==0xab;
    const bool initialized=package.data.at(begin)==0x03&&package.data.at(begin+1)==0x3c&&
        package.data.at(begin+2)==0x1a&&package.data.at(begin+3)==0xab;
    if(!direct&&!initialized)
        throw jpc::Error("Unsupported source item catalog lookup");
    auto big=[&](size_t at){if(at>end||end-at<4)throw jpc::Error("Truncated source item catalog");
        return (uint32_t(package.data[at])<<24)|(uint32_t(package.data[at+1])<<16)|(uint32_t(package.data[at+2])<<8)|package.data[at+3];};
    const auto count=big(begin+8);if(!count||count>4096||size_t(count)>(end-begin-12)/8)throw jpc::Error("Invalid source item catalog count");
    std::vector<int> result;
    for(uint32_t n=0;n<count;++n){const auto id=big(begin+12+8*n);
        if(id>65535||(!result.empty()&&id<=uint32_t(result.back())))throw jpc::Error("Invalid source item catalog ID");
        result.push_back(int(id));}
    return result;
}
inline std::vector<int> sourceItemIDs(const jpc::Package& package){return sourceLookupIDs(package,"getObjectFlags");}
inline Category sourceCategory(int id,int flags,const std::vector<int>& health,const std::vector<int>& fuel){
    if(flags&2)return Category::Weapons;
    if(flags&256)return Category::Documents;
    if(id==112||id==204)return Category::Save; // Original tape and reusable save-menu object.
    if(flags&64)return Category::Materials;
    if(std::find(health.begin(),health.end(),id)!=health.end())return Category::Health;
    if(std::find(fuel.begin(),fuel.end(),id)!=fuel.end())return Category::Fuel;
    if((flags&12)==12)return Category::Ammo; // Remaining original usable ammunition variants.
    return Category::Quest;
}
}
