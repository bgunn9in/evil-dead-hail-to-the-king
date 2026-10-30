#include "pause_menu.h"
#include <stdexcept>
int main(){
    auto require=[](bool condition){if(!condition)throw std::runtime_error("Pause menu selection failed");};
    PauseMenu menu;require(!menu.open);menu.toggle();require(menu.open&&menu.selected==0);
    menu.move(-4);require(menu.selected==4);menu.move(4);require(menu.selected==0);
    menu.move(4);require(menu.selected==1&&menu.text("game saved").find("> save game")!=std::string::npos);
    require(menu.text("game saved").find("game saved")!=std::string::npos);
    menu.move(4);menu.move(4);require(menu.selected==3&&menu.text("").find("> controls")!=std::string::npos);
    menu.move(4);require(menu.selected==4&&menu.text("").find("> quit")!=std::string::npos);
    menu.toggle();menu.toggle();require(menu.selected==0);menu.move(0);require(menu.selected==0);
}
