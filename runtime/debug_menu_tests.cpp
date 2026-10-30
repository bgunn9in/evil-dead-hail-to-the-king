#include <SDL3/SDL.h>
#include "debug_menu.h"
#include "input.h"
#include "inventory_ui.h"
#include <array>
#include <iostream>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    for(int n=1;n<argc;++n){const auto package=jpc::Package::load(argv[n]);
        const auto flags=eddebug::sourceItemIDs(package),health=eddebug::sourceLookupIDs(package,"getObjectValueHealth"),fuel=eddebug::sourceLookupIDs(package,"getObjectValueFuel");
        require(health==std::vector<int>({15,46,50,51,56,70,91,105,119}),"Native health switch keys differ");
        require(fuel==std::vector<int>({24,27,34,38,69,92,168,170,202}),"Native fuel switch keys differ");
        require(std::find(flags.begin(),flags.end(),203)!=flags.end(),"Native item flags missing reusable converter");
        std::cout<<"Native original ObjDB flags/health/fuel lookup prefixes PASS "<<argv[n]<<'\n';}
    DebugMenu menu;menu.items={{3,"ammo"},{10,"axe"}};InventoryNavigator navigation;
    // Match Main's raw menu-axis adapter; InventoryNavigator's Y is screen-down.
    auto step=[&](float x,float y,float dt=.016f,float deadzone=0.f){const auto stick=radialStick(x,y,deadzone);menu.move(navigation.update(stick.x,-stick.z,dt));};
    std::array<bool,SDL_SCANCODE_COUNT> keys{};
    auto keyboard=[&](float dt=.016f){step(float(keys[SDL_SCANCODE_D]||keys[SDL_SCANCODE_RIGHT])-float(keys[SDL_SCANCODE_A]||keys[SDL_SCANCODE_LEFT]),
        float(keys[SDL_SCANCODE_S]||keys[SDL_SCANCODE_DOWN])-float(keys[SDL_SCANCODE_W]||keys[SDL_SCANCODE_UP]),dt);};
    for(auto key:{SDL_SCANCODE_UP,SDL_SCANCODE_W,SDL_SCANCODE_DOWN,SDL_SCANCODE_S}){
        menu.selected=0;navigation.reset();keys={};keys[key]=true;keyboard();
        require(menu.selected==unsigned(key==SDL_SCANCODE_UP||key==SDL_SCANCODE_W?5:1),"raw keyboard up/down is reversed");
    }
    menu.selected=0;navigation.reset();keys={};keys[SDL_SCANCODE_UP]=true;keyboard();keyboard();
    require(menu.selected==5,"held key repeated before delay");keyboard(.36f);require(menu.selected==4,"held up did not repeat upward");
    keys={};keys[SDL_SCANCODE_DOWN]=true;keyboard();require(menu.selected==5,"direction reversal did not move down immediately");
    keys={};keyboard();keys[SDL_SCANCODE_DOWN]=true;keyboard();require(menu.selected==0,"down did not wrap to the first row");
    for(unsigned row=1;row<=6;++row){keys={};keyboard();keys[SDL_SCANCODE_DOWN]=true;keyboard();require(menu.selected==row%6,"down skipped or reversed a row");}
    menu.selected=0;navigation.reset();step(1,0);require(menu.item==1,"right item selection changed");step(0,0);step(-1,0);require(menu.item==0,"left item selection changed");
    menu.selected=1;menu.quantity=1;navigation.reset();step(-1,0);require(menu.quantity==1,"quantity lower bound changed");step(1,0);require(menu.quantity==2,"right quantity increment changed");

    require(SDL_Init(SDL_INIT_GAMEPAD),SDL_GetError());
    SDL_VirtualJoystickDesc descriptor{};SDL_INIT_INTERFACE(&descriptor);
    descriptor.type=SDL_JOYSTICK_TYPE_GAMEPAD;descriptor.naxes=SDL_GAMEPAD_AXIS_COUNT;descriptor.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;descriptor.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    descriptor.name="Evil Dead debug navigation regression";
    const auto id=SDL_AttachVirtualJoystick(&descriptor);require(id!=0,SDL_GetError());
    auto* pad=SDL_OpenGamepad(id);require(pad!=nullptr,SDL_GetError());auto* joystick=SDL_GetGamepadJoystick(pad);
    auto dpad=[&]{SDL_UpdateGamepads();step(float(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT))-float(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_LEFT)),
        float(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_DOWN))-float(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_UP)));};
    for(auto button:{SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN}){
        menu.selected=0;navigation.reset();require(SDL_SetJoystickVirtualButton(joystick,button,true),SDL_GetError());dpad();
        require(menu.selected==unsigned(button==SDL_GAMEPAD_BUTTON_DPAD_UP?5:1),"virtual dpad up/down is reversed");
        require(SDL_SetJoystickVirtualButton(joystick,button,false),SDL_GetError());dpad();
    }
    menu.selected=0;menu.item=0;navigation.reset();
    require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true),SDL_GetError());dpad();require(menu.item==1,"virtual dpad right changed");
    require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,false),SDL_GetError());dpad();
    require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true),SDL_GetError());dpad();require(menu.item==0,"virtual dpad left changed");
    require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,false),SDL_GetError());dpad();
    auto stick=[&]{SDL_UpdateGamepads();step(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX)/32768.f,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY)/32768.f,.016f,.18f);};
    for(Sint16 value:{Sint16(-32768),Sint16(32767)}){
        menu.selected=0;navigation.reset();require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,value),SDL_GetError());stick();
        require(menu.selected==unsigned(value<0?5:1),"virtual stick up/down is reversed");
        require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,0),SDL_GetError());stick();
    }
    menu.selected=0;menu.item=0;navigation.reset();
    require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,32767),SDL_GetError());stick();require(menu.item==1,"virtual stick right changed");
    require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0),SDL_GetError());stick();
    require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,-32768),SDL_GetError());stick();require(menu.item==0,"virtual stick left changed");
    require(!menu.settings.invulnerable&&!menu.settings.infiniteFuel&&!menu.settings.infiniteAmmo,"navigation changed debug effects");
    menu.items.clear();for(unsigned n=0;n<40;++n){DebugItem entry{int(n+100),"item "+std::to_string(n)};
        entry.category=n%2?eddebug::Category::Health:eddebug::Category::Fuel;entry.english="Original "+std::to_string(n);
        entry.russian=std::string("\xd2\xee\xef\xeb\xe8\xe2\xee ")+std::to_string(n);menu.items.push_back(entry);}
    menu.open=true;menu.item=0;menu.beginSelection();require(menu.wantsTextInput()&&menu.matches().size()==40,"picker did not preserve item catalog");
    SDL_Event letter{};letter.type=SDL_EVENT_KEY_DOWN;letter.key.scancode=SDL_SCANCODE_E;
    require(menu.onEvent(letter)&&menu.selecting&&menu.query.empty(),"typing E escaped into the global confirm shortcut");
    letter.key.scancode=SDL_SCANCODE_RETURN;require(!menu.onEvent(letter),"explicit Enter cannot confirm the picker");
    menu.move(1);require(menu.choice==8,"picker did not page eight items");menu.changeCategory(4);
    require(menu.category==eddebug::Category::Fuel&&menu.matches().size()==20&&menu.choice==0,"source category filtering changed");
    SDL_Event typing{};typing.type=SDL_EVENT_TEXT_INPUT;typing.text.text="ORIGINAL 2";
    require(menu.onEvent(typing)&&menu.matches().size()==6,"case-insensitive English search changed");
    menu.query.clear();typing.text.text="\xd1\x82\xd0\xbe\xd0\xbf\xd0\xbb\xd0\xb8\xd0\xb2\xd0\xbe 2";
    require(menu.onEvent(typing)&&menu.matches().size()==6,"UTF-8 to CP1251 Russian search changed");
    menu.choice=5;const auto chosen=menu.matches()[5];require(menu.confirmSelection()&&menu.item==chosen&&!menu.selecting,"picker confirm lost source ID");
    require(menu.settings.invulnerable==false,"picker granted a debug effect");menu.beginSelection();menu.query="not available";
    require(menu.matches().empty()&&!menu.confirmSelection()&&menu.selecting,"empty search selected an item");
    const auto empty=menu.presentation(true);require(empty.rows.size()==1&&empty.rows[0].text==edui::text("no matching items",true),"empty search presentation missing");
    menu.back();require(menu.open&&!menu.selecting,"picker Back closed the parent menu");menu.back();require(!menu.open,"parent Back did not close");
    const std::vector<int> health{46,70},fuel{24,69};
    require(eddebug::sourceCategory(46,60,health,fuel)==eddebug::Category::Health&&
        eddebug::sourceCategory(69,120,health,fuel)==eddebug::Category::Materials&&
        eddebug::sourceCategory(24,60,health,fuel)==eddebug::Category::Fuel&&
        eddebug::sourceCategory(3,60,health,fuel)==eddebug::Category::Ammo&&
        eddebug::sourceCategory(112,60,health,fuel)==eddebug::Category::Save,"source flags/value lookup categories changed");
    SDL_CloseGamepad(pad);require(SDL_DetachVirtualJoystick(id),SDL_GetError());SDL_Quit();
    std::cout<<"Debug navigation: raw keyboard/WASD, six rows/wrap/repeat/reversal, SDL virtual dpad/stick and unchanged horizontal/effects passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}}
