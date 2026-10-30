#include "utility_menu_presentation.h"
#include "settings_menu.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
void railBounds(const std::vector<edguipresentation::Command>& commands) {
    for(const auto& command:commands)if(const auto* page=std::get_if<edguitext::Page>(&command.value))for(const auto& glyph:page->glyphs){
        const auto crop=edguitext::croppedGlyph(*page,glyph);if(!crop.w)continue;
        assert(crop.y+crop.h<=198||crop.y>=220);
        assert(crop.y+crop.h<=352||crop.y>=380);
        assert(crop.x>=54&&crop.x+crop.w<=586&&crop.y+crop.h<=462);
    }
}
int main(int argc,char** argv){
    assert(argc==2);edutility::Presenter presenter(std::filesystem::u8path(argv[1]));
    edutility::Menu menu;menu.title="Settings";menu.footer={"Enter/A: select","Esc/B: back"};
    for(unsigned n=0;n<20;++n)menu.rows.push_back({"row"+std::to_string(n),n==17});
    menu.focus=17;menu.details={{"key1: T",false},{"key2: unbound",false},
                             {"pad1: rightstick",true},{"pad2: unbound",false}};
    auto commands=presenter.sample(menu);assert(!commands.empty());
    const auto& frame=std::get<edguipresentation::Sprite>(commands.front().value);
    assert(frame.texture==0x0b942054u&&frame.vertices.size()%3==0);
    auto original=edobjects::ObjectMesh::load(std::filesystem::u8path(argv[1])/"gui/all-models/0b142188.mesh");
    std::vector<edmesh::Vertex32> expected;
    for(const auto& v:original.vertices)if(v.part>=2&&v.part<=5)expected.push_back(v.geometry);
    assert(expected.size()==frame.vertices.size());
    for(size_t n=0;n<expected.size();++n)assert(expected[n].u==frame.vertices[n].u&&expected[n].v==frame.vertices[n].v);
    unsigned selected=0,gray=0;
    bool foundFocus=false;
    for(size_t n=1;n<commands.size();++n)for(const auto& glyph:std::get<edguitext::Page>(commands[n].value).glyphs){
        assert(glyph.w==14&&glyph.h==22&&glyph.x>=54&&glyph.x+14<=586&&glyph.y+22<=462);
        assert(glyph.y+22<=352||glyph.y>=380);
        assert(glyph.code!='>');selected+=glyph.color==0xffffff00u;gray+=glyph.color==0xff606060u;
        foundFocus|=glyph.code=='7'&&glyph.color==0xffffff00u;
    }
    assert(selected&&gray&&foundFocus);
    railBounds(commands);
    assert(presenter.sample(menu).size()==commands.size());
    auto bad=menu;bad.focus=20;assert(rejects([&]{presenter.sample(bad);}));
    bad=menu;bad.rows[17].text="$A255255255bad";assert(rejects([&]{presenter.sample(bad);}));
    bad=menu;bad.title=std::string(200,'W');const auto start=presenter.sample(bad,0),later=presenter.sample(bad,4);
    assert(std::get<edguitext::Page>(start[1].value).glyphs.size()==200&&
        std::get<edguitext::Page>(start[1].value).glyphs[0].x!=std::get<edguitext::Page>(later[1].value).glyphs[0].x);
    railBounds(start);railBounds(later);
    assert(presenter.notice("").empty());assert(presenter.notice("Settings saved").size()==1);
    const auto longNotice=presenter.notice(std::string(200,'W'));
    assert(std::get<edguitext::Page>(longNotice[0].value).nextOffset>0);railBounds(longNotice);
    assert(std::get<edguitext::Page>(presenter.notice(std::string(200,'W'),4)[0].value).nextOffset!=
        std::get<edguitext::Page>(longNotice[0].value).nextOffset);
    assert(presenter.sample(menu).size()==commands.size());
    menu.details.push_back({"trigger1: LT",false});menu.details.push_back({"trigger2: unbound",false});menu.notice="Controls saved";
    bool footerOverflow=false;
    const auto footerText=[&](double seconds){std::string text;
        for(const auto& command:presenter.sample(menu,seconds))if(const auto* page=std::get_if<edguitext::Page>(&command.value)){
            for(const auto& glyph:page->glyphs){const auto crop=edguitext::croppedGlyph(*page,glyph);
                footerOverflow|=crop.w&&crop.y>=380&&crop.y+crop.h>440;}
            if(page->bounds.top==402)for(const auto& glyph:page->glyphs)text+=char(glyph.code);
        }return text;};
    const auto firstFooter=footerText(0),secondFooter=footerText(4),repeatFooter=footerText(8);
    auto firstHelp=menu.footer[0],secondHelp=menu.footer[1];
    firstHelp.erase(std::remove(firstHelp.begin(),firstHelp.end(),' '),firstHelp.end());
    secondHelp.erase(std::remove(secondHelp.begin(),secondHelp.end(),' '),secondHelp.end());
    if(footerOverflow||firstFooter!=firstHelp||secondFooter!=secondHelp||repeatFooter!=firstHelp){
        std::cerr<<"Utility footer must show both help lines without overlapping the original bottom frame\n";return 1;}
    for(unsigned focus=0;focus<menu.rows.size();++focus){
        menu.focus=focus;for(unsigned n=0;n<menu.rows.size();++n)menu.rows[n].selected=n==focus;
        const auto visible=presenter.sample(menu);railBounds(visible);bool shown=false;
        for(const auto& command:visible)if(const auto* page=std::get_if<edguitext::Page>(&command.value)){
            std::string text;for(const auto& glyph:page->glyphs)text+=char(glyph.code);
            shown|=text==menu.rows[focus].text&&!page->glyphs.empty()&&page->glyphs.front().color==0xffffff00u;
        }
        assert(shown);
    }
    SettingsMenu settings;settings.open(VideoSettings{},{{3440,1440}});static_assert(SettingsMenu::Rows==17);
    for(unsigned focus=0;focus<SettingsMenu::Rows;++focus){settings.moveRow(int(focus)-int(settings.selectedRow()));
        for(bool russian:{false,true})railBounds(presenter.sample(settings.presentation(russian)));}
    std::cout<<"Original DC frame UV +14x22 yellow/gray centered/scrolled utility menus PASS\n";
}
