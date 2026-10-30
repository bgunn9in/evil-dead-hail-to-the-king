#pragma once
#include "input.h"
#include "inventory_ui.h"
#include "ui_strings.h"
#include "video_settings.h"
#include "utility_menu.h"

class SettingsMenu {
public:
    using Resolution=VideoResolution;
    enum Row:unsigned {Language,Original,ResolutionRow,Fullscreen,Keyboard,Gamepad,Scanlines,ScanStrength,ScanDensity,
        Film,Grain,GrainSize,Vignette,Flicker,ResetGame,Apply,Back,Rows};
    enum class Action {None,Language,Original,Keyboard,Gamepad,ResetGame,Apply,Back};
private:
    VideoSettings draft_;
    std::vector<Resolution> modes;
    InventoryNavigator navigation;
    bool open_=false,suppressFrame=false;
    unsigned row_=0,resolution_=0;
    std::string notice_;
public:
    void resetInput(){navigation.reset();suppressFrame=true;}
    void open(const VideoSettings& live,const std::vector<Resolution>& supported){
        if(!live.valid())throw std::runtime_error("Invalid live video settings");
        std::vector<Resolution> next{{0,0}};
        for(const auto& mode:supported)if(VideoSettings::validResolution(mode.width,mode.height)&&
                std::find(next.begin(),next.end(),mode)==next.end())next.push_back(mode);
        draft_=live;modes=std::move(next);row_=resolution_=0;notice_.clear();
        const auto found=std::find(modes.begin(),modes.end(),Resolution{live.width,live.height});
        if(found!=modes.end())resolution_=unsigned(found-modes.begin());
        else {draft_.width=draft_.height=0;notice_="saved resolution unavailable";}
        open_=true;resetInput();
    }
    void close(){open_=false;navigation.reset();}
    bool isOpen()const{return open_;}
    unsigned selectedRow()const{return row_;}
    unsigned selectedResolution()const{return resolution_;}
    const std::vector<Resolution>& resolutions()const{return modes;}
    const VideoSettings& draft()const{return draft_;}
    const std::string& notice()const{return notice_;}
    void setNotice(std::string value){notice_=std::move(value);}
    void languageApplied(UiLanguage language){
        if(!validUiLanguage(language))throw std::runtime_error("Invalid applied UI language");
        draft_.language=language;
    }
    void applied(const VideoSettings& live){
        if(!live.valid())throw std::runtime_error("Invalid applied video settings");
        draft_=live;const auto mode=std::find(modes.begin(),modes.end(),Resolution{live.width,live.height});
        resolution_=mode==modes.end()?0:unsigned(mode-modes.begin());notice_="settings applied";resetInput();
    }
    void moveRow(int delta){
        if(!open_||!delta)return;
        row_=unsigned((int(row_)+delta%int(Rows)+int(Rows))%int(Rows));notice_.clear();
    }
    void moveValue(int delta){
        if(!open_||!delta)return;
        if(row_==ResolutionRow){
            const int count=int(modes.size());resolution_=unsigned((int(resolution_)+delta%count+count)%count);
            draft_.width=modes[resolution_].width;draft_.height=modes[resolution_].height;
        }else if(row_==Fullscreen)draft_.fullscreen=!draft_.fullscreen;
        else if(row_==Scanlines)draft_.scanlines=!draft_.scanlines;
        else if(row_==Film)draft_.film=!draft_.film;
        else if(row_==Language)draft_.language=russianUi(draft_.language)?UiLanguage::English:UiLanguage::Russian;
        else {
            const auto adjust=[&](float& value,float step,float low,float high){
                value=std::clamp(std::round((value+float(delta)*step)/step)*step,low,high);
            };
            switch(row_){
            case ScanStrength:adjust(draft_.scanStrength,.01f,0,.5f);break;
            case ScanDensity:adjust(draft_.lineCount,30,120,480);break;
            case Grain:adjust(draft_.grainStrength,.005f,0,.15f);break;
            case GrainSize:adjust(draft_.grainSize,.25f,.5f,4);break;
            case Vignette:adjust(draft_.vignette,.005f,0,.25f);break;
            case Flicker:adjust(draft_.flicker,.002f,0,.05f);break;
            default:break;
            }
        }
        notice_.clear();
    }
    Action activate(){
        if(!open_)return Action::None;
        switch(row_){
        case Language:moveValue(1);return Action::Language;
        case Original:return Action::Original;
        case Keyboard:return Action::Keyboard;
        case Gamepad:return Action::Gamepad;
        case ResetGame:draft_=VideoSettings{};resolution_=0;notice_.clear();return Action::ResetGame;
        case Apply:return Action::Apply;
        case Back:close();return Action::Back;
        default:moveValue(1);return Action::None;
        }
    }
    Action step(Vec2 direction,float dt,bool confirmEdge,bool backEdge){
        if(!open_)return Action::None;
        if(suppressFrame){suppressFrame=false;navigation.reset();return Action::None;}
        if(!std::isfinite(dt)||dt<0){navigation.reset();return Action::None;}
        if(backEdge){close();return Action::Back;}
        const int delta=navigation.update(direction.x,direction.z,dt);
        if(delta==4||delta==-4)moveRow(delta/4);
        else if(delta){moveValue(delta);if(row_==Language)return Action::Language;}
        return confirmEdge?activate():Action::None;
    }
    std::string rowText(unsigned row,bool russian=false)const{
        if(row>=Rows)throw std::out_of_range("Unknown settings row");
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        const char* labels[]={"language","original: sound/vibration/credits","resolution","fullscreen","keyboard","gamepad",
            "scanlines","scanline strength","scanline count","film","film grain","grain size","vignette","film flicker","reset game settings","apply","back"};
        auto label=t(labels[row]);
        if(row==ResolutionRow)label+=": "+(draft_.width?std::to_string(draft_.width)+"x"+std::to_string(draft_.height):t("auto"));
        else if(row==Language)label+=": "+(russianUi(draft_.language)?edui::text("russian",true):std::string("English"));
        else if(row==Fullscreen||row==Scanlines||row==Film){
            const bool enabled=row==Fullscreen?draft_.fullscreen:row==Scanlines?draft_.scanlines:draft_.film;
            label+=": "+t(enabled?"on":"off");
        }else {
            float value=0;bool parameter=true,percent=true;
            switch(row){
            case ScanStrength:value=draft_.scanStrength;break;
            case ScanDensity:value=draft_.lineCount;percent=false;break;
            case Grain:value=draft_.grainStrength;break;
            case GrainSize:value=draft_.grainSize;percent=false;break;
            case Vignette:value=draft_.vignette;break;
            case Flicker:value=draft_.flicker;break;
            default:parameter=false;break;
            }
            if(parameter){
                const int hundredths=int(std::lround(value*(percent?1000:100)));
                const int unit=percent?10:100;
                std::string number=std::to_string(hundredths/unit);
                if(hundredths%unit)number+="."+(unit==100&&hundredths%unit<10?std::string("0"):std::string())+std::to_string(hundredths%unit);
                label+=": "+number+(percent?"%":"");
            }
        }
        return label;
    }
    edutility::Menu presentation(bool russian=false)const{
        edutility::Menu menu;menu.title=edui::text("settings",russian);menu.focus=row_;
        for(unsigned row=0;row<Rows;++row)menu.rows.push_back({rowText(row,russian),row==row_});
        menu.footer={edui::text("arrows/stick  enter/a  esc/b",russian),edui::text("back discards unsaved changes",russian)};
        menu.notice=edui::text(notice_,russian);return menu;
    }
    std::string text(bool russian=false)const{
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        std::string result;
        auto line=[&](std::string value){
            for(auto& c:value)if(c=='\r'||c=='\n')c=' ';
            if(value.size()>34)value.resize(34);result+=value+'\n';
        };
        line(t("settings"));line(t("arrows/stick  enter/a  esc/b"));
        const unsigned begin=std::min(row_>4?row_-4:0,unsigned(Rows)-9);
        for(unsigned row=begin;row<begin+9;++row){
            line((row==row_?"> ":"  ")+rowText(row,russian));
        }
        if(!notice_.empty())line(t(notice_));
        line(t("back discards unsaved changes"));
        if(result.size()>512)throw std::runtime_error("Settings HUD exceeds text limit");
        return result;
    }
};
