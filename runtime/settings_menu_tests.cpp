#include "settings_menu.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static void row(SettingsMenu& menu,unsigned target){menu.moveRow(int(target)-int(menu.selectedRow()));assert(menu.selectedRow()==target);}
static void layout(const std::string& text){
    assert(text.size()<=512);size_t start=0;
    for(size_t end=text.find('\n');end!=std::string::npos;end=text.find('\n',start)){assert(end-start<=34);start=end+1;}
}
int main(){
    using Action=SettingsMenu::Action;
    VideoSettings live;const auto original=live.serialize();SettingsMenu menu;
    const std::vector<VideoResolution> supported{{1920,1080},{1280,720},{1920,1080},{0,0},{1,1},{16385,1080}};
    menu.open(live,supported);assert(menu.resolutions().size()==3&&menu.selectedResolution()==0);
    assert(menu.text().find("resolution: auto")!=std::string::npos);
    assert(menu.step({},.033f,true,false)==Action::None&&menu.selectedRow()==SettingsMenu::Language);
    assert(menu.activate()==Action::Language&&menu.draft().language==UiLanguage::English);
    assert(menu.rowText(SettingsMenu::Language,true).find("English")!=std::string::npos);
    menu.languageApplied(UiLanguage::Russian);row(menu,SettingsMenu::Original);
    assert(menu.activate()==Action::Original&&menu.isOpen());
    row(menu,SettingsMenu::ResolutionRow);menu.moveValue(1);assert(menu.draft().width==1920&&menu.draft().height==1080);
    menu.moveValue(1);assert(menu.draft().width==1280);menu.moveValue(1);assert(menu.draft().width==0);
    menu.moveValue(-1);assert(menu.draft().width==1280);
    row(menu,SettingsMenu::Fullscreen);menu.activate();assert(menu.draft().fullscreen);
    row(menu,SettingsMenu::Scanlines);menu.moveValue(-1);assert(menu.draft().scanlines);
    row(menu,SettingsMenu::Film);menu.activate();assert(menu.draft().film);
    row(menu,SettingsMenu::Language);assert(menu.activate()==Action::Language&&menu.draft().language==UiLanguage::English);
    menu.moveValue(-1);assert(menu.draft().language==UiLanguage::Russian);
    const auto pending=menu.draft();menu.languageApplied(UiLanguage::English);
    auto expected=pending;expected.language=UiLanguage::English;assert(menu.draft()==expected);
    menu.languageApplied(pending.language);assert(menu.draft()==pending);
    bool invalid=false;try{menu.languageApplied(UiLanguage(255));}catch(const std::runtime_error&){invalid=true;}
    assert(invalid&&menu.draft()==pending);
    for(unsigned parameter:{SettingsMenu::ScanStrength,SettingsMenu::ScanDensity,SettingsMenu::Grain,
                           SettingsMenu::GrainSize,SettingsMenu::Vignette,SettingsMenu::Flicker}){
        row(menu,parameter);const auto before=menu.draft();menu.moveValue(1);
        assert(menu.draft().valid()&&!(menu.draft()==before));
        for(unsigned i=0;i<1000;++i)menu.moveValue(-1);
        assert(menu.draft().valid());
        for(unsigned i=0;i<1000;++i)menu.moveValue(1);
        assert(menu.draft().valid());
    }
    for(bool russian:{false,true})for(unsigned target=0;target<SettingsMenu::Rows;++target){
        row(menu,target);const auto native=menu.presentation(russian);
        assert(native.rows.size()==SettingsMenu::Rows&&native.focus==target&&native.rows[target].selected);
        for(const auto& line:native.rows)assert(line.text.size()<=38);
        layout(menu.text(russian));
    }
    row(menu,SettingsMenu::Keyboard);assert(menu.activate()==Action::Keyboard&&menu.isOpen());
    row(menu,SettingsMenu::Gamepad);assert(menu.activate()==Action::Gamepad&&menu.isOpen());
    row(menu,SettingsMenu::Apply);assert(menu.activate()==Action::Apply&&live.serialize()==original&&menu.isOpen());
    row(menu,SettingsMenu::ResetGame);assert(menu.activate()==Action::ResetGame&&menu.draft()==VideoSettings{}&&live.serialize()==original);
    for(bool russian:{false,true})layout(menu.text(russian));
    assert(menu.step({},.033f,false,true)==Action::Back&&!menu.isOpen()&&live.serialize()==original);
    menu.open(live,supported);assert(menu.draft()==live);
    menu.step({},.033f,false,false);
    assert(menu.step({0,1},.033f,false,false)==Action::None&&menu.selectedRow()==SettingsMenu::Original);
    menu.step({0,1},.1f,false,false);assert(menu.selectedRow()==SettingsMenu::Original);
    menu.step({0,1},.3f,false,false);assert(menu.selectedRow()==SettingsMenu::ResolutionRow);
    menu.resetInput();assert(menu.step({},.033f,true,true)==Action::None&&menu.isOpen());
    menu.step({},-.1f,true,true);assert(menu.isOpen());
    VideoSettings unplugged{3840,2160,true,false,true};menu.open(unplugged,supported);
    assert(!menu.draft().width&&!menu.draft().height&&unplugged.width==3840&&menu.draft().film&&menu.draft().fullscreen);
    assert(menu.notice()=="saved resolution unavailable");
    VideoSettings applied{1280,720,false,true,false};menu.applied(applied);assert(menu.draft()==applied&&menu.selectedResolution()==2);
    assert(menu.step({},.033f,true,false)==Action::None); // Mode change cannot reuse its Apply pulse.
    menu.setNotice(std::string(2048,'x')+"\nignored");
    layout(menu.text());layout(menu.text(true));
    row(menu,SettingsMenu::Back);assert(menu.activate()==Action::Back&&!menu.isOpen());
    menu.open(live,{});menu.step({},0,false,false);row(menu,SettingsMenu::ResolutionRow);
    menu.moveValue(1);assert(menu.resolutions().size()==1&&!menu.draft().width);
    menu.open(live,supported);menu.step({},0,false,false);
    assert(menu.step({1,0},.033f,true,false)==Action::Language&&menu.draft().language==UiLanguage::English);
    assert(menu.selectedRow()==SettingsMenu::Language&&live.serialize()==original);
    std::cout<<"Settings draft/navigation/actions/Apply boundary/desktop fallback/bounded HUD PASS\n";
}
