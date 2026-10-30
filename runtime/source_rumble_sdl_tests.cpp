#include "source_rumble_sdl.h"
#include <stdexcept>
#include <vector>
#include <array>
#include <iostream>
static void require(bool value){if(!value)throw std::runtime_error("SDL native rumble bridge differs");}
int main(){try{
    require(SDL_Init(SDL_INIT_GAMEPAD));
    std::vector<std::array<Uint16,2>> sent;
    SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.name="Native rumble test";
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.userdata=&sent;desc.Rumble=[](void* data,Uint16 low,Uint16 high){
        static_cast<decltype(sent)*>(data)->push_back({low,high});return true;};
    const auto id=SDL_AttachVirtualJoystick(&desc);require(id!=0);SDL_UpdateGamepads();
    auto* pad=SDL_OpenGamepad(id);require(pad&&edrumble::available(pad)&&!edrumble::available(pad,1));
    edrumble::Feedback output;sent.clear();output.command({0,0,150});require(sent.empty());
    output.update(pad,true);require(!sent.empty()&&sent.back()==std::array<Uint16,2>{65535,65535});
    output.update(pad,false);require(sent.back()==std::array<Uint16,2>{0,0});
    output.update(pad,true);require(sent.back()==std::array<Uint16,2>{65535,65535});
    output.command({0,32,1});output.update(pad,true);require(sent.back()==std::array<Uint16,2>{18724,18724});
    output.command({0,0,0});output.update(pad,true);require(sent.back()==std::array<Uint16,2>{0,0});
    output.command({0,0,150});output.update(pad,true);output.reset(pad);
    require(sent.back()==std::array<Uint16,2>{0,0});output.update(pad,true);require(sent.back()==std::array<Uint16,2>{0,0});
    output.command({0,0,150});output.update(pad,true);SDL_Delay(150);SDL_UpdateGamepads();
    require(sent.back()==std::array<Uint16,2>{0,0});
    output.reset(pad);
    edrumble::Chainsaw saw;saw.weapon=17;output.update(pad,true,saw,1000);
    saw.running=true;saw.phase=1;output.update(pad,true,saw,1001);const auto start=sent.back()[0];
    output.command({0,0,0});output.update(pad,true,saw,1002);require(sent.back()[0]==start);
    saw.phase=2;output.update(pad,true,saw,1250);const auto idle=sent.back()[0];require(idle>0&&idle<start);
    saw.phase=3;saw.revTimer=90;output.update(pad,true,saw,1300);const auto rev=sent.back()[0];require(rev>idle);
    saw.revTimer=89;output.update(pad,true,saw,1400);require(sent.back()[0]==rev);
    saw.revTimer=88;output.update(pad,true,saw,1500);require(sent.back()[0]==idle); // Held gas never retriggers a pulse.
    saw.revTimer=90;output.update(pad,true,saw,1510);require(sent.back()[0]==rev);
    saw.attackPitch=2500;output.update(pad,true,saw,1700);require(sent.back()[0]>idle);
    saw.attackPitch=2375;output.update(pad,true,saw,1800);require(sent.back()[0]>idle);
    saw.attackPitch=2250;output.update(pad,true,saw,1900);require(sent.back()[0]==idle);
    output.command({0,0,150});output.update(pad,true,saw,1910);require(sent.back()[0]==65535);
    output.command({});output.update(pad,true,saw,1911);require(sent.back()[0]==idle);
    saw.gameplay=false;output.update(pad,true,saw,1912);require(sent.back()[0]==0);
    saw.gameplay=true;output.update(pad,true,saw,1930);require(sent.back()[0]==idle);
    output.update(pad,false,saw,2000);require(sent.back()[0]==0);
    output.update(pad,true,saw,2001);require(sent.back()[0]==idle);
    saw.enabled=false;output.command({0,0,150});output.update(pad,true,saw,2010);require(sent.back()[0]==0);
    saw.enabled=true;output.update(pad,true,saw,2011);require(sent.back()[0]==idle);
    saw.running=false;saw.phase=0;output.update(pad,true,saw,2020);require(sent.back()[0]==0);
    output.reset(pad);saw.running=true;saw.phase=2;output.update(pad,true,saw,2030);require(sent.back()[0]==idle);
    saw.revving=true;output.update(pad,true,saw,2100);require(sent.back()[0]>idle&&sent.back()[0]<rev);
    output.update(pad,true,saw,2500);require(sent.back()[0]>idle);
    saw.revving=false;output.update(pad,true,saw,2510);require(sent.back()[0]==idle);
    output.reset(pad);SDL_CloseGamepad(pad);require(SDL_DetachVirtualJoystick(id));SDL_Quit();
    std::cout<<"Native SDL rumble capability, source power, pause/resume, stop/reset and finite pulse passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}
