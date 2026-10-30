#pragma once
#include <string>
#include "ui_strings.h"
#include "utility_menu.h"
struct PauseMenu {
    static constexpr unsigned Count=5;
    bool open=false;
    unsigned selected=0;
    void toggle(){open=!open;selected=0;}
    void move(int delta) {
        if(delta)selected=unsigned((int(selected)+(delta>0?1:int(Count)-1))%int(Count));
    }
    edutility::Menu presentation(const std::string& notice,bool russian=false)const {
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        edutility::Menu menu;menu.title=t("paused");menu.focus=selected;
        const char* labels[]={"resume","save game","load game","controls","quit"};
        for(unsigned n=0;n<Count;++n)menu.rows.push_back({t(labels[n]),n==selected});
        menu.footer={t("arrows/stick: choose"),t("e/a: select  esc/b: back")};
        menu.notice=t(notice);return menu;
    }
    std::string text(const std::string& notice,bool russian=false)const {
        std::string result=edui::text("paused",russian)+'\n';
        const char* labels[]={"resume","save game","load game","controls","quit"};
        for(unsigned i=0;i<Count;++i)result+=(i==selected?"> ":"  ")+edui::text(labels[i],russian)+"\n";
        return result+edui::text("arrows/stick: choose",russian)+'\n'+edui::text("e/a: select  esc/b: back",russian)+'\n'+edui::text(notice,russian).substr(0,34);
    }
};
