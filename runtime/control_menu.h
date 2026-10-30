#pragma once
#include "control_settings.h"
#include "inventory_ui.h"
#include "ui_strings.h"
#include "utility_menu.h"

class ControlMenu {
    ControlSettings draft_;
    InventoryNavigator navigation;
    bool open_=false,capturing_=false,suppressFrame=false,confirmPulse=false,backPulse=false,focused=true;
    unsigned row_=0,slot_=0;
    SDL_Scancode blockedKey=SDL_SCANCODE_UNKNOWN;
    SDL_GamepadButton blockedButton=SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadAxis blockedAxis=SDL_GAMEPAD_AXIS_INVALID;
    std::string notice_;
    static std::string ascii(const char* value) {
        if(!value||!*value) return "unbound";
        std::string result;
        for(const unsigned char c:std::string(value)) {
            if(c=='@')result+="at";
            else if(c=='$')result+="dollar";
            else if(c=='`')result+="grave";
            else if(c=='^')result+="caret";
            else result.push_back(c>=32&&c<127?char(c>='A'&&c<='Z'?c-'A'+'a':c):'?');
        }
        return result;
    }
    static std::string bindingName(const ControlBinding& binding,unsigned slot,bool russian) {
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        if(slot>=4)return binding.axes[slot-4]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER?"LT":
            binding.axes[slot-4]==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER?"RT":t("unbound");
        if(slot>=2) {
            static constexpr const char* labels[]={"A","B","X","Y","Back","Guide","Start","L3","R3","LB","RB",
                "d-pad up","d-pad down","d-pad left","d-pad right","button 1","right paddle 1","left paddle 1",
                "right paddle 2","left paddle 2","touchpad","button 2","button 3","button 4","button 5","button 6"};
            static_assert(std::size(labels)==SDL_GAMEPAD_BUTTON_COUNT,"Name every supported gamepad button");
            const int button=int(binding.buttons[slot-2]);
            return button>=0&&button<SDL_GAMEPAD_BUTTON_COUNT?t(labels[button]):t("unbound");
        }
        const auto key=binding.keys[slot];
        switch(key) {
        case SDL_SCANCODE_LCTRL:return t("left Ctrl");case SDL_SCANCODE_RCTRL:return t("right Ctrl");
        case SDL_SCANCODE_LSHIFT:return t("left Shift");case SDL_SCANCODE_RSHIFT:return t("right Shift");
        case SDL_SCANCODE_LALT:return t("left Alt");case SDL_SCANCODE_RALT:return t("right Alt");
        case SDL_SCANCODE_LGUI:return t("left Win");case SDL_SCANCODE_RGUI:return t("right Win");
        case SDL_SCANCODE_SPACE:return t("space");
        case SDL_SCANCODE_UP:return t("up");case SDL_SCANCODE_DOWN:return t("down");
        case SDL_SCANCODE_LEFT:return t("left");case SDL_SCANCODE_RIGHT:return t("right");
        case SDL_SCANCODE_RETURN:return "Enter";case SDL_SCANCODE_ESCAPE:return "Esc";
        case SDL_SCANCODE_TAB:return "Tab";case SDL_SCANCODE_BACKSPACE:return "Backspace";
        case SDL_SCANCODE_DELETE:return "Del";case SDL_SCANCODE_INSERT:return "Ins";
        case SDL_SCANCODE_HOME:return "Home";case SDL_SCANCODE_END:return "End";
        case SDL_SCANCODE_PAGEUP:return "Page Up";case SDL_SCANCODE_PAGEDOWN:return "Page Down";
        case SDL_SCANCODE_CAPSLOCK:return "Caps Lock";
        default: {
            auto name=ascii(SDL_GetScancodeName(key));
            if(name.size()==1&&name[0]>='a'&&name[0]<='z')name[0]=char(name[0]-'a'+'A');
            else if(name.size()>1&&name[0]=='f'&&std::all_of(name.begin()+1,name.end(),[](char c){return c>='0'&&c<='9';}))name[0]='F';
            return t(name);
        }
        }
    }
    unsigned slots() const { return row_<unsigned(ControlAction::Fullscreen)||(row_>=unsigned(ControlAction::ChainsawRev)&&row_<ControlActionCount)?6:2; }
    void clearInput() { confirmPulse=backPulse=false; navigation.reset(); }
    void captured(BindingResult result,const SDL_Event& event) {
        if(result==BindingResult::Conflict) notice_="already bound; try another";
        else if(result==BindingResult::Invalid) notice_="invalid binding; try another";
        else if(result==BindingResult::Applied||result==BindingResult::Cancelled) {
            capturing_=false; notice_=result==BindingResult::Applied?"binding changed; save to apply":"capture cancelled";
            if(event.type==SDL_EVENT_KEY_DOWN) blockedKey=event.key.scancode;
            if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) blockedButton=SDL_GamepadButton(event.gbutton.button);
            if(event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION)blockedAxis=SDL_GamepadAxis(event.gaxis.axis);
        }
        clearInput(); suppressFrame=true;
    }
public:
    static constexpr unsigned DeadzoneRow=ControlActionCount,ResetKeyboardRow=ControlActionCount+1,
        ResetGamepadRow=ControlActionCount+2,SaveRow=ControlActionCount+3,Rows=ControlActionCount+4;
    void open(const ControlSettings& live,unsigned preferredSlot=0) {
        draft_=live; open_=true; capturing_=false; focused=true; row_=0;slot_=std::min(preferredSlot,slots()-1);
        blockedKey=SDL_SCANCODE_UNKNOWN; blockedButton=SDL_GAMEPAD_BUTTON_INVALID;blockedAxis=SDL_GAMEPAD_AXIS_INVALID;
        notice_.clear(); suppressFrame=true; clearInput();
    }
    void close() { open_=capturing_=false; clearInput(); }
    void gamepadChanged() { blockedButton=SDL_GAMEPAD_BUTTON_INVALID;blockedAxis=SDL_GAMEPAD_AXIS_INVALID; clearInput(); suppressFrame=true; }
    void back() { if(capturing_) {capturing_=false;notice_="capture cancelled";suppressFrame=true;clearInput();} else close(); }
    bool isOpen() const { return open_; }
    bool capturing() const { return capturing_; }
    unsigned selectedRow() const { return row_; }
    unsigned selectedSlot() const { return slot_; }
    const ControlSettings& draft() const { return draft_; }
    const std::string& notice() const { return notice_; }
    void moveRow(int delta) {
        if(!open_||capturing_||!delta) return;
        const int value=(int(row_)+delta%int(Rows)+int(Rows))%int(Rows); row_=unsigned(value);
        slot_=std::min(slot_,slots()-1); notice_.clear();
    }
    void moveSlot(int delta) {
        if(!open_||capturing_||!delta) return;
        if(row_==DeadzoneRow) {
            const float next=std::round((draft_.deadzone()+float(delta)*0.01f)*100)/100;
            draft_.setDeadzone(std::clamp(next,0.0f,0.5f)); notice_="save to apply deadzone";
        } else if(row_<ControlActionCount) slot_=unsigned((int(slot_)+delta%int(slots())+int(slots()))%int(slots()));
    }
    void clearBinding() {
        if(!open_||capturing_||row_>=ControlActionCount) return;
        const auto action=ControlAction(row_);
        const auto result=slot_<2?draft_.bindKey(action,slot_,SDL_SCANCODE_UNKNOWN):
            slot_<4?draft_.bindButton(action,slot_-2,SDL_GAMEPAD_BUTTON_INVALID):
                     draft_.bindAxis(action,slot_-4,SDL_GAMEPAD_AXIS_INVALID);
        if(result==BindingResult::Applied) notice_="binding cleared; save to apply";
    }
    bool save(ControlSettings& live,const std::filesystem::path& preferenceDirectory) {
        if(!open_||capturing_) return false;
        try {
            draft_.save(preferenceDirectory); live=draft_; close(); notice_="controls saved"; return true;
        } catch(const std::exception&) { notice_="save failed; old controls kept"; return false; }
    }
    void activate(ControlSettings& live,const std::filesystem::path& preferenceDirectory) {
        if(!open_||capturing_) return;
        if(row_<ControlActionCount) { capturing_=true; notice_.clear(); navigation.reset(); }
        else if(row_==ResetKeyboardRow) { draft_.resetKeyboardDefaults(); notice_="keyboard defaults ready; save"; }
        else if(row_==ResetGamepadRow) { draft_.resetGamepadDefaults(); notice_="gamepad defaults ready; save"; }
        else if(row_==SaveRow) save(live,preferenceDirectory);
    }
    bool onEvent(const SDL_Event& event) {
        if(!open_) return false;
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST||event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {
            focused=event.type==SDL_EVENT_WINDOW_FOCUS_GAINED;blockedAxis=SDL_GAMEPAD_AXIS_INVALID; clearInput(); suppressFrame=true; return false;
        }
        const bool key=event.type==SDL_EVENT_KEY_DOWN,button=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN,
                   axis=event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION;
        if(event.type==SDL_EVENT_KEY_UP&&event.key.scancode==blockedKey) blockedKey=SDL_SCANCODE_UNKNOWN;
        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_UP&&event.gbutton.button==blockedButton) blockedButton=SDL_GAMEPAD_BUTTON_INVALID;
        if(axis&&event.gaxis.axis==blockedAxis&&event.gaxis.value<=16383)blockedAxis=SDL_GAMEPAD_AXIS_INVALID;
        if(!key&&!button&&!axis) return false;
        if(!focused||suppressFrame||(key&&event.key.scancode==blockedKey)||(button&&event.gbutton.button==blockedButton)||
           (axis&&event.gaxis.axis==blockedAxis)) return true;
        if(capturing_) {
            if((key&&event.key.scancode==SDL_SCANCODE_ESCAPE)||
               (button&&event.gbutton.button==SDL_GAMEPAD_BUTTON_START&&(slot_<2||slot_>=4)))
                captured(BindingResult::Cancelled,event);
            else if((slot_<2&&key)||(slot_>=2&&slot_<4&&button)||
                    (slot_>=4&&axis&&(event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER||event.gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)&&event.gaxis.value>16383))
                captured(draft_.capture(ControlAction(row_),slot_%2,event),event);
            return true;
        }
        if(key&&!event.key.repeat) {
            if(event.key.scancode==SDL_SCANCODE_ESCAPE) backPulse=true;
            if(event.key.scancode==SDL_SCANCODE_E||event.key.scancode==SDL_SCANCODE_RETURN||event.key.scancode==SDL_SCANCODE_KP_ENTER) confirmPulse=true;
            if(event.key.scancode==SDL_SCANCODE_DELETE||event.key.scancode==SDL_SCANCODE_BACKSPACE) clearBinding();
        }
        if(key) {
            if(event.key.scancode==SDL_SCANCODE_UP) moveRow(-1);
            if(event.key.scancode==SDL_SCANCODE_DOWN) moveRow(1);
            if(event.key.scancode==SDL_SCANCODE_LEFT) moveSlot(-1);
            if(event.key.scancode==SDL_SCANCODE_RIGHT) moveSlot(1);
        }
        if(button) {
            if(event.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH) confirmPulse=true;
            if(event.gbutton.button==SDL_GAMEPAD_BUTTON_EAST||event.gbutton.button==SDL_GAMEPAD_BUTTON_START) backPulse=true;
            if(event.gbutton.button==SDL_GAMEPAD_BUTTON_WEST) clearBinding();
        }
        return true;
    }
    void step(Vec2 padNavigationXY,float dt,ControlSettings& live,const std::filesystem::path& preferenceDirectory) {
        if(!open_) return;
        if(suppressFrame) { suppressFrame=false; clearInput(); return; }
        if(!focused||capturing_) { clearInput(); return; }
        if(backPulse) { close(); return; }
        const int delta=navigation.update(padNavigationXY.x,padNavigationXY.z,dt);
        if(delta==4||delta==-4) moveRow(delta/4); else moveSlot(delta);
        const bool confirm=confirmPulse; confirmPulse=backPulse=false;
        if(confirm) activate(live,preferenceDirectory);
    }
    edutility::Menu presentation(bool russian=false)const {
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        edutility::Menu menu;menu.title=t("controls");menu.focus=row_;
        for(unsigned i=0;i<Rows;++i) {
            std::string label=i<ControlActionCount?t(controlActionName(ControlAction(i))):i==DeadzoneRow?
                t("deadzone ")+std::to_string(int(std::lround(draft_.deadzone()*100)))+"%":
                i==ResetKeyboardRow?t("reset keyboard"):i==ResetGamepadRow?t("reset gamepad"):t("save controls");
            menu.rows.push_back({std::move(label),i==row_});
        }
        if(row_<ControlActionCount) {
            const auto& binding=draft_.binding(ControlAction(row_));
            for(unsigned i=0;i<slots();++i) {
                menu.details.push_back({t(i<2?"key ":i<4?"pad ":"trigger ")+std::to_string(i%2+1)+": "+bindingName(binding,i,russian),i==slot_});
            }
        }
        menu.footer={capturing_?t(slot_<2?"press a key; esc/start: cancel":slot_<4?"press a button; esc: cancel":"press a trigger; esc/start: cancel"):
                                  t("del/backspace/x: clear binding"),t("back discards unsaved changes")};
        menu.notice=t(notice_);return menu;
    }
    std::string text(bool russian=false) const {
        std::string result;
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        auto line=[&](std::string value) { if(value.size()>34)value.resize(34); result+=value+'\n'; };
        line(t("controls")); line(t("arrows/stick: row / alias slot")); line(t("e/a: change   esc/b: back"));
        const unsigned begin=std::min(row_>2?row_-2:0,Rows-5);
        for(unsigned i=begin;i<begin+5;++i) {
            std::string label=i<ControlActionCount?t(controlActionName(ControlAction(i))):i==DeadzoneRow?
                t("deadzone ")+std::to_string(int(std::lround(draft_.deadzone()*100)))+"%":i==ResetKeyboardRow?t("reset keyboard"):i==ResetGamepadRow?t("reset gamepad"):t("save controls");
            line((i==row_?"> ":"  ")+label);
        }
        if(row_<ControlActionCount) {
            const auto& binding=draft_.binding(ControlAction(row_));
            for(unsigned i=0;i<slots();++i) {
                line((i==slot_?"> ":"  ")+t(i<2?"key ":i<4?"pad ":"trigger ")+std::to_string(i%2+1)+": "+bindingName(binding,i,russian));
            }
            if(row_<4) line(t("fixed axis: left stick"));
        } else if(row_==DeadzoneRow) line(t("left/right: adjust by 1%"));
        if(capturing_) line(t(slot_<2?"press a key; esc/start: cancel":slot_<4?"press a button; esc: cancel":"press a trigger; esc/start: cancel"));
        else line(t("del/backspace/x: clear binding"));
        if(!notice_.empty()) line(t(notice_));
        line(t("back discards unsaved changes"));
        if(result.size()>512) throw std::runtime_error("Controls HUD exceeds text limit");
        return result;
    }
};
