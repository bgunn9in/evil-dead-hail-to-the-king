#include "control_menu.h"
#include "debug_menu.h"
#include "pause_menu.h"
#include "utility_menu_presentation.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);edutility::Presenter presenter(std::filesystem::u8path(argv[1]));
    ControlSettings live;ControlMenu controls;controls.open(live,2);
    const auto original=controls.draft().serialize();
    for(unsigned row=0;row<ControlMenu::Rows;++row) {
        controls.moveRow(int(row)-int(controls.selectedRow()));
        for(bool russian:{false,true}) {
            const auto menu=controls.presentation(russian);
            assert(menu.focus==row&&menu.rows.size()==ControlMenu::Rows&&menu.rows[row].selected);
            assert(menu.details.empty()||menu.details[controls.selectedSlot()].selected);
            assert(!presenter.sample(menu).empty());
        }
        assert(controls.draft().serialize()==original);
    }
    controls.open(live,2);controls.activate(live,{});
    assert(controls.capturing());auto capture=controls.presentation(true);
    assert(capture.footer[0]==edui::text("press a button; esc: cancel",true));
    assert(capture.details[2].selected&&!presenter.sample(capture).empty());
    DebugMenu debug;debug.items={{46,edui::text("first aid kit",true)}};debug.selected=2;
    debug.settings.invulnerable=true;debug.quantity=5;
    const auto view=debug.presentation(true);
    assert(view.rows.size()==6&&view.focus==2&&view.rows[2].selected);
    assert(view.rows[1].text==edui::text("quantity: ",true)+"5");
    assert(view.rows[2].text==edui::text("invulnerability: ",true)+edui::text("on",true));
    assert(!presenter.sample(view).empty()&&debug.settings.invulnerable&&debug.quantity==5);
    PauseMenu pause;pause.toggle();pause.move(1);
    const auto paused=pause.presentation("settings applied",true);
    assert(paused.rows.size()==5&&paused.focus==1&&paused.rows[1].selected);
    assert(paused.notice==edui::text("settings applied",true)&&!presenter.sample(paused).empty());
    const edutility::Menu* menus[]={&view,&paused,&capture};
    for(const auto* menu:menus)for(const auto& row:menu->rows)
        assert(row.text.rfind("> ",0)!=0);
    const char* oldCurrency=SDL_GetScancodeName(SDL_SCANCODE_CURRENCYUNIT);assert(SDL_SetScancodeName(SDL_SCANCODE_CURRENCYUNIT,"Keypad $"));
    for(const auto code:{SDL_SCANCODE_KP_AT,SDL_SCANCODE_CURRENCYUNIT}){
        controls.close();controls.open(live);controls.step({},0,live,{});controls.moveRow(int(ControlAction::Use));controls.activate(live,{});
        SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=code;assert(controls.onEvent(event));assert(!controls.capturing());
        assert(controls.draft().binding(ControlAction::Use).keys[0]==code);const auto literal=controls.presentation();
        const auto safe=presenter.sample(literal);assert(!safe.empty());for(const auto& command:safe)if(const auto* page=std::get_if<edguitext::Page>(&command.value))for(const auto& glyph:page->glyphs)assert(glyph.x>=54&&glyph.x+14<=586&&glyph.y+22<=462);
    }
    assert(SDL_SetScancodeName(SDL_SCANCODE_CURRENCYUNIT,oldCurrency));
    std::ifstream fontInput(std::filesystem::u8path(argv[1])/"font.txt",std::ios::binary);const auto font=edfont::Metadata::read(fontInput);
    ControlSettings everyKey;for(unsigned action=0;action<ControlActionCount;++action)for(unsigned slot=0;slot<2;++slot)
        assert(everyKey.bindKey(ControlAction(action),slot,SDL_SCANCODE_UNKNOWN)==BindingResult::Applied);
    unsigned audited=0;
    for(int code=1;code<SDL_SCANCODE_COUNT;++code)if(*SDL_GetScancodeName(SDL_Scancode(code))){
        assert(everyKey.bindKey(ControlAction::Use,0,SDL_Scancode(code))==BindingResult::Applied);
        controls.open(everyKey);controls.moveRow(int(ControlAction::Use));
        for(bool russian:{false,true})for(const auto& command:presenter.sample(controls.presentation(russian)))
            if(const auto* page=std::get_if<edguitext::Page>(&command.value))for(const auto& glyph:page->glyphs)
                assert(edfont::glyph(font.charset,glyph.code)!=std::string::npos);
        ++audited;
    }
    assert(audited>200);
    const auto oldNotice=std::string("raw\n\r\t$A255255255@")+char(0)+char(127)+char(152)+std::string(1000,'x');debug.notice=oldNotice;
    for(bool russian:{false,true}){const auto failure=debug.presentation(russian);assert(failure.notice.size()==oldNotice.size()&&debug.notice==oldNotice);
        for(uint8_t code:failure.notice)assert(edfont::cp1251Byte(code)&&code!='$'&&code!='@');
        const auto visible=[&](double seconds){std::string text;const auto commands=presenter.sample(failure,seconds);assert(!commands.empty());
            for(const auto& command:commands)if(const auto* page=std::get_if<edguitext::Page>(&command.value))for(const auto& glyph:page->glyphs){
                const auto crop=edguitext::croppedGlyph(*page,glyph);if(crop.w&&crop.h){text+=char(glyph.code);
                    if(page->clip)assert(crop.x>=page->bounds.left&&crop.y>=page->bounds.top&&crop.x+crop.w<=page->bounds.right&&crop.y+crop.h<=page->bounds.bottom);}}
            return text;};
        assert(visible(0)!=visible(20));}
    std::cout<<"Semantic Controls all rows/slots/capture + Debug flags + Pause notices native presentation PASS\n";
}
