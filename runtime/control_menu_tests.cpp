#include "control_menu.h"
#include "debug_menu.h"
#include "pause_menu.h"
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <random>

namespace fs=std::filesystem;
SDL_Event key(SDL_Scancode code) {SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=code;return e;}
SDL_Event button(SDL_GamepadButton code,bool down=true) {SDL_Event e{};e.type=down?SDL_EVENT_GAMEPAD_BUTTON_DOWN:SDL_EVENT_GAMEPAD_BUTTON_UP;e.gbutton.button=Uint8(code);return e;}
savefile::Bytes raw(const fs::path& path) {std::ifstream in(path,std::ios::binary);return savefile::Bytes(std::istreambuf_iterator<char>(in),{});}
void row(ControlMenu& menu,unsigned target) {menu.moveRow(int(target)-int(menu.selectedRow()));assert(menu.selectedRow()==target);}
void layout(const std::string& text) {
    assert(text.size()<=512);std::size_t start=0;
    for(std::size_t end=text.find('\n');end!=std::string::npos;end=text.find('\n',start)){assert(end-start<=34);start=end+1;}
}
void bindingDisplayTests() {
    ControlSettings blank;
    for(unsigned n=0;n<ControlActionCount;++n)for(unsigned slot=0;slot<2;++slot) {
        assert(blank.bindKey(ControlAction(n),slot,SDL_SCANCODE_UNKNOWN)==BindingResult::Applied);
        assert(blank.bindButton(ControlAction(n),slot,SDL_GAMEPAD_BUTTON_INVALID)==BindingResult::Applied);
    }
    ControlMenu menu;
    const auto check=[&](const ControlSettings& controls,unsigned detail,const std::string& english,const std::string& russian){
        const auto before=controls.serialize();menu.open(controls,detail);row(menu,unsigned(ControlAction::Use));
        const auto en=menu.presentation(),ru=menu.presentation(true);
        assert(en.details[detail].text==edui::text(detail<2?"key ":"pad ")+std::to_string(detail%2+1)+": "+english);
        assert(ru.details[detail].text==edui::text(detail<2?"key ":"pad ",true)+std::to_string(detail%2+1)+": "+russian);
        assert(menu.text().find(english)!=std::string::npos&&menu.text(true).find(russian)!=std::string::npos);
        assert(controls.serialize()==before&&menu.draft().serialize()==before);
    };
    for(const auto& entry:std::array<std::pair<SDL_GamepadButton,const char*>,10>{{
        {SDL_GAMEPAD_BUTTON_SOUTH,"A"},{SDL_GAMEPAD_BUTTON_EAST,"B"},{SDL_GAMEPAD_BUTTON_WEST,"X"},
        {SDL_GAMEPAD_BUTTON_NORTH,"Y"},{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,"LB"},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,"RB"},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,"L3"},{SDL_GAMEPAD_BUTTON_RIGHT_STICK,"R3"},
        {SDL_GAMEPAD_BUTTON_START,"Start"},{SDL_GAMEPAD_BUTTON_BACK,"Back"}}}) {
        auto controls=blank;assert(controls.bindButton(ControlAction::Use,0,entry.first)==BindingResult::Applied);
        check(controls,2,entry.second,entry.second);
    }
    for(int button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button){
        auto controls=blank;assert(controls.bindButton(ControlAction::Use,0,SDL_GamepadButton(button))==BindingResult::Applied);
        menu.open(controls,2);row(menu,unsigned(ControlAction::Use));
        const auto english=menu.presentation().details[2].text,russian=menu.presentation(true).details[2].text;
        assert(english.find("unbound")==std::string::npos&&english.find('_')==std::string::npos);
        for(const auto* token:{"leftshoulder","rightshoulder","leftstick","rightstick","dpup","dpdown","dpleft","dpright"})
            assert(english.find(token)==std::string::npos&&russian.find(token)==std::string::npos);
        for(uint8_t code:russian)assert(edfont::cp1251Byte(code));
    }
    for(const auto& entry:std::array<std::pair<SDL_Scancode,const char*>,4>{{
        {SDL_SCANCODE_LSHIFT,"left Shift"},{SDL_SCANCODE_LCTRL,"left Ctrl"},
        {SDL_SCANCODE_SPACE,"space"},{SDL_SCANCODE_UP,"up"}}}){
        auto controls=blank;assert(controls.bindKey(ControlAction::Use,0,entry.first)==BindingResult::Applied);
        const auto translated=edui::text(entry.second,true);assert(translated!=entry.second);
        check(controls,0,entry.second,translated);
    }
    for(const auto& entry:std::array<std::pair<SDL_GamepadButton,const char*>,3>{{
        {SDL_GAMEPAD_BUTTON_DPAD_UP,"d-pad up"},{SDL_GAMEPAD_BUTTON_TOUCHPAD,"touchpad"},
        {SDL_GAMEPAD_BUTTON_MISC1,"button 1"}}}){
        auto controls=blank;assert(controls.bindButton(ControlAction::Use,0,entry.first)==BindingResult::Applied);
        const auto translated=edui::text(entry.second,true);assert(translated!=entry.second);
        check(controls,2,entry.second,translated);
    }
}
int main() {
    bindingDisplayTests();
    const auto directory=fs::temp_directory_path()/("evil-dead-control-menu-test-"+std::to_string(std::random_device{}()));
    assert(fs::create_directory(directory));
    ControlSettings live;const auto original=live.serialize();live.save(directory);
    ControlMenu preferred;preferred.open(live,2);assert(preferred.selectedSlot()==2);
    preferred.open(live,999);assert(preferred.selectedSlot()==5);
    preferred.open(live);assert(preferred.selectedSlot()==0);
    ControlMenu menu;menu.open(live);menu.step({},0,live,directory);
    row(menu,unsigned(ControlAction::Use));menu.activate(live,directory);assert(menu.capturing());
    assert(menu.onEvent(key(SDL_SCANCODE_G)));assert(!menu.capturing()&&menu.draft().binding(ControlAction::Use).keys[0]==SDL_SCANCODE_G);
    assert(menu.onEvent(key(SDL_SCANCODE_E)));menu.step({},0,live,directory);
    assert(!menu.capturing()&&menu.isOpen()&&live.serialize()==original);
    menu.activate(live,directory);const auto beforeCancel=menu.draft().serialize();
    assert(!menu.save(live,directory)&&live.serialize()==original);
    assert(menu.onEvent(button(SDL_GAMEPAD_BUTTON_START)));assert(!menu.capturing()&&menu.isOpen()&&menu.draft().serialize()==beforeCancel);
    menu.step({},0,live,directory);menu.onEvent(button(SDL_GAMEPAD_BUTTON_START,false));
    menu.back();assert(!menu.isOpen()&&live.serialize()==original);

    menu.open(live);menu.step({},0,live,directory);
    row(menu,unsigned(ControlAction::Use)); // DC B/East is already the ordinary Use button.
    row(menu,unsigned(ControlAction::Use));menu.moveSlot(2);assert(menu.selectedSlot()==2);
    menu.activate(live,directory);assert(menu.onEvent(button(SDL_GAMEPAD_BUTTON_EAST)));
    assert(!menu.capturing()&&menu.draft().binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_EAST);
    menu.step({},0,live,directory);menu.onEvent(button(SDL_GAMEPAD_BUTTON_EAST));menu.step({},0,live,directory);assert(menu.isOpen());
    menu.onEvent(button(SDL_GAMEPAD_BUTTON_EAST,false));menu.onEvent(button(SDL_GAMEPAD_BUTTON_EAST));menu.step({},0,live,directory);
    assert(!menu.isOpen()&&live.serialize()==original);

    menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::Use));
    menu.onEvent(key(SDL_SCANCODE_DELETE));assert(menu.draft().binding(ControlAction::Use).keys[0]==SDL_SCANCODE_UNKNOWN);
    row(menu,unsigned(ControlAction::PrimaryAttack));menu.activate(live,directory);menu.onEvent(key(SDL_SCANCODE_E));menu.step({},0,live,directory);
    assert(menu.draft().binding(ControlAction::PrimaryAttack).keys[0]==SDL_SCANCODE_E&&live.serialize()==original);
    menu.close();menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::PrimaryAttack));menu.moveSlot(2);
    menu.onEvent(button(SDL_GAMEPAD_BUTTON_WEST));assert(menu.draft().binding(ControlAction::PrimaryAttack).buttons[0]==SDL_GAMEPAD_BUTTON_INVALID);
    row(menu,unsigned(ControlAction::Use));menu.activate(live,directory);menu.onEvent(button(SDL_GAMEPAD_BUTTON_SOUTH));menu.step({},0,live,directory);
    assert(menu.draft().binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_SOUTH&&live.serialize()==original);
    menu.close();

    menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::ChainsawRev));menu.moveSlot(2);assert(menu.selectedSlot()==2);
    menu.activate(live,directory);menu.onEvent(button(SDL_GAMEPAD_BUTTON_MISC1));menu.step({},0,live,directory);
    assert(menu.draft().binding(ControlAction::ChainsawRev).buttons[0]==SDL_GAMEPAD_BUTTON_MISC1&&live.serialize()==original);
    menu.close();
    menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::Taunt));menu.moveSlot(2);assert(menu.selectedSlot()==2);
    menu.activate(live,directory);menu.onEvent(button(SDL_GAMEPAD_BUTTON_MISC1));menu.step({},0,live,directory);
    assert(menu.draft().binding(ControlAction::Taunt).buttons[0]==SDL_GAMEPAD_BUTTON_MISC1&&live.serialize()==original);
    assert(menu.text(true).find(edui::text("taunt",true))!=std::string::npos&&edui::text("taunt",true)!="taunt");menu.close();

    for(auto action:{ControlAction::Forward,ControlAction::Backward,ControlAction::TurnLeft,ControlAction::TurnRight}){
        menu.open(live,2);menu.step({},0,live,directory);row(menu,unsigned(action));assert(menu.selectedSlot()==2);
        menu.moveSlot(1);menu.activate(live,directory);menu.onEvent(button(SDL_GAMEPAD_BUTTON_MISC1));menu.step({},0,live,directory);
        assert(menu.draft().binding(action).buttons[1]==SDL_GAMEPAD_BUTTON_MISC1&&live.serialize()==original);
        row(menu,unsigned(ControlAction::ChainsawRev));menu.moveSlot(1);assert(menu.selectedSlot()==4);menu.clearBinding();
        row(menu,unsigned(action));menu.moveSlot(1);assert(menu.selectedSlot()==5);menu.activate(live,directory);
        SDL_Event axis{};axis.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;axis.gaxis.axis=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;axis.gaxis.value=22000;
        menu.onEvent(axis);menu.step({},0,live,directory);assert(!menu.capturing()&&menu.draft().binding(action).axes[1]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        menu.moveSlot(1);assert(menu.selectedSlot()==0);menu.activate(live,directory);menu.onEvent(key(SDL_SCANCODE_G));menu.step({},0,live,directory);
        assert(!menu.capturing()&&menu.draft().binding(action).keys[0]==SDL_SCANCODE_G&&menu.draft().binding(action).buttons[1]==SDL_GAMEPAD_BUTTON_MISC1);
        assert(menu.presentation().details.size()==6);layout(menu.text());menu.close();
    }

    // Independent button and trigger aliases: capture never replaces the other kind.
    menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::ChainsawRev));menu.moveSlot(4);menu.clearBinding();
    row(menu,unsigned(ControlAction::Use));menu.moveSlot(1);assert(menu.selectedSlot()==5);menu.activate(live,directory);
    SDL_Event trigger{};trigger.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;trigger.gaxis.axis=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;trigger.gaxis.value=10000;
    assert(menu.onEvent(trigger)&&menu.capturing());trigger.gaxis.axis=SDL_GAMEPAD_AXIS_LEFTX;trigger.gaxis.value=30000;
    assert(menu.onEvent(trigger)&&menu.capturing());
    trigger.gaxis.axis=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;trigger.gaxis.value=22000;
    const auto triggerRev=menu.draft().revision();assert(menu.onEvent(trigger)&&!menu.capturing());
    assert(menu.draft().revision()!=triggerRev&&menu.draft().binding(ControlAction::Use).axes[1]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    assert(menu.draft().binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_EAST&&live.serialize()==original);
    menu.step({},0,live,directory);menu.clearBinding();assert(menu.draft().binding(ControlAction::Use).axes[1]==SDL_GAMEPAD_AXIS_INVALID);
    assert(menu.draft().binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_EAST);menu.close();
    menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::Inventory));menu.moveSlot(2);menu.clearBinding();
    row(menu,unsigned(ControlAction::Use));menu.moveSlot(1);menu.activate(live,directory);const auto startRev=menu.draft().revision();
    assert(menu.onEvent(button(SDL_GAMEPAD_BUTTON_START))&&!menu.capturing()&&menu.isOpen());
    assert(menu.draft().revision()!=startRev&&menu.draft().binding(ControlAction::Use).buttons[1]==SDL_GAMEPAD_BUTTON_START);
    assert(menu.draft().binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_EAST);
    assert(menu.draft().binding(ControlAction::Inventory).buttons[0]==SDL_GAMEPAD_BUTTON_INVALID&&live.serialize()==original);menu.close();
    menu.open(live);menu.step({},0,live,directory);row(menu,ControlMenu::DeadzoneRow);
    menu.moveSlot(100);assert(menu.draft().deadzone()==0.5f);menu.moveSlot(-100);assert(menu.draft().deadzone()==0);
    row(menu,ControlMenu::ResetGamepadRow);menu.activate(live,directory);assert(menu.draft().serialize()==original);
    row(menu,unsigned(ControlAction::Use));menu.activate(live,directory);menu.onEvent(key(SDL_SCANCODE_G));menu.step({},0,live,directory);
    row(menu,ControlMenu::DeadzoneRow);menu.moveSlot(5);const auto customDeadzone=menu.draft().deadzone();
    row(menu,ControlMenu::ResetGamepadRow);menu.activate(live,directory);
    assert(menu.draft().binding(ControlAction::Use).keys[0]==SDL_SCANCODE_G&&menu.draft().deadzone()==ControlSettings{}.deadzone());
    row(menu,ControlMenu::DeadzoneRow);menu.moveSlot(5);
    row(menu,ControlMenu::ResetKeyboardRow);menu.activate(live,directory);
    assert(menu.draft().binding(ControlAction::Use).keys==live.binding(ControlAction::Use).keys&&menu.draft().deadzone()==customDeadzone);
    row(menu,unsigned(ControlAction::Use));menu.activate(live,directory);menu.onEvent(key(SDL_SCANCODE_H));menu.step({},0,live,directory);
    row(menu,ControlMenu::SaveRow);menu.activate(live,directory);assert(!menu.isOpen()&&live.binding(ControlAction::Use).keys[0]==SDL_SCANCODE_H);
    ControlSettings disk;assert(disk.load(directory)&&disk.serialize()==live.serialize());
    const auto saved=live.serialize(),file=raw(directory/"controls.eds");
    menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::Aim));menu.activate(live,directory);
    menu.onEvent(key(SDL_SCANCODE_V));menu.step({},0,live,directory);assert(!menu.capturing());
    const auto permissions=fs::status(directory/"controls.eds").permissions();
    fs::permissions(directory/"controls.eds",permissions&~(fs::perms::owner_write|fs::perms::group_write|fs::perms::others_write));
    assert(!menu.save(live,directory)&&menu.isOpen()&&live.serialize()==saved&&raw(directory/"controls.eds")==file);
    fs::permissions(directory/"controls.eds",permissions);
    for(unsigned i=0;i<ControlMenu::Rows;++i){row(menu,i);const auto draft=menu.draft().serialize();
        const auto english=menu.text(),russian=menu.text(true);layout(english);layout(russian);
        assert(english==menu.text(false)&&english!=russian&&menu.draft().serialize()==draft);
        if(i<ControlActionCount)assert(russian.find(edui::text(controlActionName(ControlAction(i)),true))!=std::string::npos);}
    menu.close();menu.open(live);menu.step({},0,live,directory);menu.activate(live,directory);
    assert(menu.capturing());layout(menu.text(true));assert(menu.text(true).find(edui::text("press a key; esc/start: cancel",true))!=std::string::npos);menu.back();
    PauseMenu pause;pause.toggle();const auto paused=pause.text("game saved");
    assert(paused=="paused\n> resume\n  save game\n  load game\n  controls\n  quit\narrows/stick: choose\ne/a: select  esc/b: back\ngame saved");
    layout(pause.text("game saved",true));assert(pause.text("game saved",true).find(edui::text("game saved",true))!=std::string::npos);
    DebugMenu debug;debug.items={{65535,std::string(100,char(0xc0))}};debug.notice="not received: full or locked";
    for(unsigned n=0;n<6;++n){debug.selected=n;layout(debug.text(true));assert(debug.text()==debug.text(false));}
    assert(edui::text("paused")=="paused"&&edui::text("paused",true)==std::string("\317\340\363\347\340"));
    assert(edui::text("unmapped diagnostic",true)=="unmapped diagnostic");
    for(const auto& entry:edui::entries){assert(!entry.russian.empty()&&entry.russian.size()<=512);for(uint8_t code:entry.russian)assert(edfont::cp1251Byte(code));}
    menu.close();menu.open(live);menu.step({},0,live,directory);
    menu.step({0,1},0.01f,live,directory);assert(menu.selectedRow()==1);
    menu.step({0,1},0.2f,live,directory);assert(menu.selectedRow()==1);
    menu.step({0,1},0.16f,live,directory);assert(menu.selectedRow()==2);
    const char* oldCurrency=SDL_GetScancodeName(SDL_SCANCODE_CURRENCYUNIT);
    assert(SDL_SetScancodeName(SDL_SCANCODE_CURRENCYUNIT,"Keypad $"));
    for(const auto code:{SDL_SCANCODE_KP_AT,SDL_SCANCODE_CURRENCYUNIT}){
        menu.close();menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::Use));menu.activate(live,directory);menu.onEvent(key(code));
        assert(!menu.capturing()&&menu.draft().binding(ControlAction::Use).keys[0]==code);
        const auto view=menu.presentation();assert(view.details[0].text.find(code==SDL_SCANCODE_KP_AT?"keypad at":"keypad dollar")!=std::string::npos);
        for(const auto& detail:view.details)assert(detail.text.find_first_of("@$")==std::string::npos);
        layout(menu.text());assert(menu.text().find_first_of("@$")==std::string::npos);
    }
    assert(SDL_SetScancodeName(SDL_SCANCODE_CURRENCYUNIT,oldCurrency));
    for(const auto code:{SDL_SCANCODE_GRAVE,SDL_SCANCODE_KP_POWER}){
        menu.close();menu.open(live);menu.step({},0,live,directory);row(menu,unsigned(ControlAction::Use));menu.activate(live,directory);menu.onEvent(key(code));
        assert(!menu.capturing()&&menu.draft().binding(ControlAction::Use).keys[0]==code);
        const auto view=menu.presentation();assert(view.details[0].text.find(code==SDL_SCANCODE_GRAVE?"grave":"keypad caret")!=std::string::npos);
    }
    fs::remove(directory/"controls.eds");assert(fs::remove(directory));
}
