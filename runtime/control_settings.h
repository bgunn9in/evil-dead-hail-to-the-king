#pragma once
#include <SDL3/SDL.h>
#include "input.h"
#include "save_file.h"
#include "state_bytes.h"
#include <array>
#include <atomic>
#include <utility>

enum class ControlAction : unsigned {
    MoveUp,MoveDown,MoveLeft,MoveRight,Run,Aim,Use,Inventory,Pause,
    PrimaryAttack,SecondaryAttack,Fullscreen,QuickSave,QuickLoad,ChainsawRev,ChainsawStop,Taunt,
    Forward,Backward,TurnLeft,TurnRight,Count
};
constexpr unsigned ControlActionCount=unsigned(ControlAction::Count);
inline const char* controlActionName(ControlAction action) {
    constexpr const char* names[]={"move up","move down","move left","move right","run","aim","use","inventory","pause",
        "primary attack","secondary attack","fullscreen","quick save","quick load","chainsaw rev","chainsaw stop","taunt",
        "forward","backward","turn left","turn right"};
    return unsigned(action)<ControlActionCount?names[unsigned(action)]:"unknown";
}
struct ControlBinding {
    std::array<SDL_Scancode,2> keys{SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UNKNOWN};
    std::array<SDL_GamepadButton,2> buttons{SDL_GAMEPAD_BUTTON_INVALID,SDL_GAMEPAD_BUTTON_INVALID};
    std::array<SDL_GamepadAxis,2> axes{SDL_GAMEPAD_AXIS_INVALID,SDL_GAMEPAD_AXIS_INVALID};
};
enum class BindingResult { Applied,Conflict,Invalid,Cancelled,Ignored };

class ControlSettings {
    inline static std::atomic<std::uint64_t> nextRevision{1};
    std::array<ControlBinding,ControlActionCount> bindings{};
    float deadzone_=0.18f;
    std::uint64_t revision_=nextRevision.fetch_add(1,std::memory_order_relaxed);
    void changed() { revision_=nextRevision.fetch_add(1,std::memory_order_relaxed); }
    static bool validKey(int key) {
        return key==SDL_SCANCODE_UNKNOWN||(key>SDL_SCANCODE_UNKNOWN&&key<SDL_SCANCODE_COUNT&&*SDL_GetScancodeName(SDL_Scancode(key)));
    }
    static bool validButton(int button) { return button>=SDL_GAMEPAD_BUTTON_INVALID&&button<SDL_GAMEPAD_BUTTON_COUNT; }
    static bool validAxis(int axis) { return axis==SDL_GAMEPAD_AXIS_INVALID||axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER||axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; }
    static bool padAction(unsigned action) { return action<unsigned(ControlAction::Fullscreen)||(action>=unsigned(ControlAction::ChainsawRev)&&action<ControlActionCount); }
    static bool validDeadzone(float value) { return std::isfinite(value)&&value>=0&&value<=0.5f; }
    static BindingResult validate(const std::array<ControlBinding,ControlActionCount>& data,float deadzone) {
        if(!validDeadzone(deadzone)) return BindingResult::Invalid;
        std::array<bool,SDL_SCANCODE_COUNT> keys{};
        std::array<bool,SDL_GAMEPAD_BUTTON_COUNT> buttons{};
        std::array<bool,SDL_GAMEPAD_AXIS_COUNT> axes{};
        for(unsigned action=0;action<ControlActionCount;++action) {
            for(auto key:data[action].keys) {
                if(!validKey(int(key))) return BindingResult::Invalid;
                if(key!=SDL_SCANCODE_UNKNOWN) { if(keys[key]) return BindingResult::Conflict; keys[key]=true; }
            }
            for(auto button:data[action].buttons) {
                if(!validButton(int(button))||(button!=SDL_GAMEPAD_BUTTON_INVALID&&!padAction(action))) return BindingResult::Invalid;
                if(button!=SDL_GAMEPAD_BUTTON_INVALID) { if(buttons[button]) return BindingResult::Conflict; buttons[button]=true; }
            }
            for(auto axis:data[action].axes) {
                if(!validAxis(int(axis))||(axis!=SDL_GAMEPAD_AXIS_INVALID&&!padAction(action)))return BindingResult::Invalid;
                if(axis!=SDL_GAMEPAD_AXIS_INVALID){if(axes[axis])return BindingResult::Conflict;axes[axis]=true;}
            }
        }
        return BindingResult::Applied;
    }
public:
    ControlSettings() {
        auto set=[&](ControlAction action,SDL_Scancode key,SDL_Scancode alias=SDL_SCANCODE_UNKNOWN,
                     SDL_GamepadButton button=SDL_GAMEPAD_BUTTON_INVALID,SDL_GamepadButton other=SDL_GAMEPAD_BUTTON_INVALID) {
            bindings[unsigned(action)]={{key,alias},{button,other}};
        };
        set(ControlAction::MoveUp,SDL_SCANCODE_W,SDL_SCANCODE_UP);
        set(ControlAction::MoveDown,SDL_SCANCODE_S,SDL_SCANCODE_DOWN);
        set(ControlAction::MoveLeft,SDL_SCANCODE_A,SDL_SCANCODE_LEFT);
        set(ControlAction::MoveRight,SDL_SCANCODE_D,SDL_SCANCODE_RIGHT);
        set(ControlAction::Run,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT,SDL_GAMEPAD_BUTTON_LEFT_STICK);
        set(ControlAction::Aim,SDL_SCANCODE_SPACE,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        set(ControlAction::Use,SDL_SCANCODE_E,SDL_SCANCODE_RETURN,SDL_GAMEPAD_BUTTON_EAST);
        set(ControlAction::Inventory,SDL_SCANCODE_I,SDL_SCANCODE_TAB,SDL_GAMEPAD_BUTTON_START);
        set(ControlAction::Pause,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_BACK);
        set(ControlAction::PrimaryAttack,SDL_SCANCODE_J,SDL_SCANCODE_Q,SDL_GAMEPAD_BUTTON_SOUTH);
        set(ControlAction::SecondaryAttack,SDL_SCANCODE_K,SDL_SCANCODE_C,SDL_GAMEPAD_BUTTON_WEST);
        set(ControlAction::Fullscreen,SDL_SCANCODE_F11);
        set(ControlAction::QuickSave,SDL_SCANCODE_F5);
        set(ControlAction::QuickLoad,SDL_SCANCODE_F9);
        set(ControlAction::ChainsawRev,SDL_SCANCODE_R);
        set(ControlAction::ChainsawStop,SDL_SCANCODE_F,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        set(ControlAction::Taunt,SDL_SCANCODE_T,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_NORTH);
        set(ControlAction::Forward,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_DPAD_UP);
        set(ControlAction::Backward,SDL_SCANCODE_Z,SDL_SCANCODE_LCTRL,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        set(ControlAction::TurnLeft,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        set(ControlAction::TurnRight,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UNKNOWN,SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        bindings[unsigned(ControlAction::Run)].axes[0]=SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        bindings[unsigned(ControlAction::ChainsawRev)].axes[0]=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    }
    const ControlBinding& binding(ControlAction action) const {
        if(unsigned(action)>=ControlActionCount) throw std::out_of_range("Unknown control action");
        return bindings[unsigned(action)];
    }
    float deadzone() const { return deadzone_; }
    std::uint64_t revision() const { return revision_; }
    bool setDeadzone(float value) {
        if(!validDeadzone(value)) return false;
        if(value!=deadzone_) { deadzone_=value; changed(); }
        return true;
    }
    BindingResult bindKey(ControlAction action,unsigned slot,SDL_Scancode key) {
        if(unsigned(action)>=ControlActionCount||slot>=2) return BindingResult::Invalid;
        auto candidate=bindings; candidate[unsigned(action)].keys[slot]=key;
        const auto result=validate(candidate,deadzone_);
        if(result==BindingResult::Applied&&bindings[unsigned(action)].keys[slot]!=key) { bindings=candidate; changed(); }
        return result;
    }
    BindingResult bindButton(ControlAction action,unsigned slot,SDL_GamepadButton button) {
        if(unsigned(action)>=ControlActionCount||slot>=2) return BindingResult::Invalid;
        auto candidate=bindings; candidate[unsigned(action)].buttons[slot]=button;
        const auto result=validate(candidate,deadzone_);
        if(result==BindingResult::Applied&&bindings[unsigned(action)].buttons[slot]!=button) { bindings=candidate; changed(); }
        return result;
    }
    BindingResult bindAxis(ControlAction action,unsigned slot,SDL_GamepadAxis axis) {
        if(unsigned(action)>=ControlActionCount||slot>=2)return BindingResult::Invalid;
        auto candidate=bindings;candidate[unsigned(action)].axes[slot]=axis;
        const auto result=validate(candidate,deadzone_);
        if(result==BindingResult::Applied&&bindings[unsigned(action)].axes[slot]!=axis){bindings=candidate;changed();}
        return result;
    }
    BindingResult capture(ControlAction action,unsigned slot,const SDL_Event& event) {
        if(unsigned(action)>=ControlActionCount||slot>=2) return BindingResult::Invalid;
        if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat) {
            if(event.key.scancode==SDL_SCANCODE_ESCAPE) return BindingResult::Cancelled;
            return event.key.scancode==SDL_SCANCODE_UNKNOWN?BindingResult::Invalid:bindKey(action,slot,event.key.scancode);
        }
        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            return event.gbutton.button<SDL_GAMEPAD_BUTTON_COUNT?bindButton(action,slot,SDL_GamepadButton(event.gbutton.button)):BindingResult::Invalid;
        if(event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&event.gaxis.value>16383)
            return validAxis(event.gaxis.axis)&&event.gaxis.axis!=SDL_GAMEPAD_AXIS_INVALID?
                bindAxis(action,slot,SDL_GamepadAxis(event.gaxis.axis)):BindingResult::Ignored;
        return BindingResult::Ignored;
    }
    void resetKeyboardDefaults() {
        const ControlSettings defaults;
        for(unsigned action=0;action<ControlActionCount;++action)bindings[action].keys=defaults.bindings[action].keys;
        changed();
    }
    void resetGamepadDefaults() {
        const ControlSettings defaults;
        for(unsigned action=0;action<ControlActionCount;++action){bindings[action].buttons=defaults.bindings[action].buttons;bindings[action].axes=defaults.bindings[action].axes;}
        deadzone_=defaults.deadzone_;changed();
    }
    void resetDefaults() { const ControlSettings defaults; bindings=defaults.bindings; deadzone_=defaults.deadzone_; changed(); }
    savefile::Bytes serialize() const {
        statebytes::Writer out; out.text("EDCONTROL1"); out.put<uint32_t>(3); out.put<uint32_t>(ControlActionCount); out.put(deadzone_);
        for(const auto& item:bindings) { for(auto key:item.keys) out.put<int32_t>(int(key)); for(auto button:item.buttons) out.put<int32_t>(int(button));for(auto axis:item.axes)out.put<int32_t>(int(axis)); }
        return std::move(out.bytes);
    }
    void restore(const savefile::Bytes& bytes) {
        if(bytes.size()>4096) throw std::runtime_error("Control settings exceed byte limit");
        statebytes::Reader in{bytes};
        if(in.text()!="EDCONTROL1")throw std::runtime_error("Unsupported control settings");
        const auto version=in.get<uint32_t>();
        const unsigned count=in.get<uint32_t>();
        if(version<1||version>3||(version==3?count!=ControlActionCount:version==2?(count!=17&&count!=ControlActionCount):(count!=14&&count!=16&&count!=17)))
            throw std::runtime_error("Unsupported control settings");
        const float zone=in.get<float>(); const auto defaults=ControlSettings{}.bindings;auto candidate=defaults;
        std::array<ControlBinding,ControlActionCount> oldDefaults{};
        for(unsigned n=0;n<4;++n)oldDefaults[n].buttons[0]=SDL_GamepadButton(SDL_GAMEPAD_BUTTON_DPAD_UP+n);
        oldDefaults[unsigned(ControlAction::Use)].buttons[0]=SDL_GAMEPAD_BUTTON_SOUTH;
        oldDefaults[unsigned(ControlAction::Inventory)].buttons={SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_BACK};
        oldDefaults[unsigned(ControlAction::Pause)].buttons[0]=SDL_GAMEPAD_BUTTON_START;
        oldDefaults[unsigned(ControlAction::PrimaryAttack)].buttons[0]=SDL_GAMEPAD_BUTTON_WEST;
        oldDefaults[unsigned(ControlAction::SecondaryAttack)].buttons[0]=SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        oldDefaults[unsigned(ControlAction::ChainsawRev)].buttons[0]=SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        oldDefaults[unsigned(ControlAction::ChainsawStop)].buttons[0]=SDL_GAMEPAD_BUTTON_EAST;
        oldDefaults[unsigned(ControlAction::Taunt)].buttons[0]=SDL_GAMEPAD_BUTTON_RIGHT_STICK;
        auto previousDefaults=defaults;
        for(unsigned n=0;n<4;++n)previousDefaults[n].buttons[0]=oldDefaults[n].buttons[0];
        previousDefaults[unsigned(ControlAction::SecondaryAttack)].buttons[1]=SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        bool oldDefaultPad=true,oldDefaultKeyboard=version<3;
        for(unsigned action=0;action<count;++action) {
            auto& item=candidate[action];
            for(auto& key:item.keys) {
                const int value=in.get<int32_t>(); if(!validKey(value)) throw std::runtime_error("Invalid saved scancode"); key=SDL_Scancode(value);
            }
            auto oldKeys=defaults[action].keys;
            if(action==unsigned(ControlAction::PrimaryAttack)||action==unsigned(ControlAction::SecondaryAttack)||action==unsigned(ControlAction::Backward))oldKeys[1]=SDL_SCANCODE_UNKNOWN;
            oldDefaultKeyboard=oldDefaultKeyboard&&item.keys==oldKeys;
            for(auto& button:item.buttons) {
                const int value=in.get<int32_t>(); if(!validButton(value)) throw std::runtime_error("Invalid saved gamepad button"); button=SDL_GamepadButton(value);
            }
            item.axes={SDL_GAMEPAD_AXIS_INVALID,SDL_GAMEPAD_AXIS_INVALID};
            if(version>=2)for(auto& axis:item.axes){const int value=in.get<int32_t>();if(!validAxis(value))throw std::runtime_error("Invalid saved trigger");axis=SDL_GamepadAxis(value);}
            oldDefaultPad=oldDefaultPad&&item.buttons==(version==1?oldDefaults:previousDefaults)[action].buttons&&
                (version==1||item.axes==previousDefaults[action].axes);
        }
        in.end();
        if(oldDefaultKeyboard)for(unsigned action=0;action<ControlActionCount;++action)candidate[action].keys=defaults[action].keys;
        if(oldDefaultPad&&(version==1||count==17)) {
            for(unsigned action=0;action<ControlActionCount;++action){
                candidate[action].buttons=defaults[action].buttons;candidate[action].axes=defaults[action].axes;
            }
        }else if(version==1) {
            for(unsigned action=0;action<17;++action){
                candidate[action].axes={SDL_GAMEPAD_AXIS_INVALID,SDL_GAMEPAD_AXIS_INVALID};
                if(action>=count)candidate[action].buttons=oldDefaults[action].buttons;
            }
            candidate[unsigned(ControlAction::Aim)].axes[0]=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            candidate[unsigned(ControlAction::Run)].axes[0]=SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        }
        // Old custom bindings win; only conflicting newly added defaults are cleared.
        for(unsigned action=count;action<ControlActionCount;++action) {
            for(auto& key:candidate[action].keys)if(key!=SDL_SCANCODE_UNKNOWN)
                for(unsigned old=0;old<count;++old)if(std::find(candidate[old].keys.begin(),candidate[old].keys.end(),key)!=candidate[old].keys.end()) {key=SDL_SCANCODE_UNKNOWN;break;}
            for(auto& button:candidate[action].buttons)if(button!=SDL_GAMEPAD_BUTTON_INVALID)
                for(unsigned old=0;old<count;++old)if(std::find(candidate[old].buttons.begin(),candidate[old].buttons.end(),button)!=candidate[old].buttons.end()) {button=SDL_GAMEPAD_BUTTON_INVALID;break;}
            for(auto& axis:candidate[action].axes)if(axis!=SDL_GAMEPAD_AXIS_INVALID)
                for(unsigned old=0;old<count;++old)if(std::find(candidate[old].axes.begin(),candidate[old].axes.end(),axis)!=candidate[old].axes.end()) {axis=SDL_GAMEPAD_AXIS_INVALID;break;}
        }
        if(validate(candidate,zone)!=BindingResult::Applied) throw std::runtime_error("Invalid or conflicting saved controls");
        bindings=candidate; deadzone_=zone; changed();
    }
    void save(const std::filesystem::path& preferenceDirectory) const { savefile::atomicWrite(preferenceDirectory,"controls.eds",serialize(),4096); }
    bool load(const std::filesystem::path& preferenceDirectory) {
        std::error_code error; const auto status=std::filesystem::symlink_status(preferenceDirectory/"controls.eds",error);
        if(status.type()==std::filesystem::file_type::not_found) return false;
        if(error) throw std::runtime_error("Cannot inspect saved control settings");
        restore(savefile::read(preferenceDirectory,"controls.eds",4096)); return true;
    }
};

struct ControlRawState {
    std::array<bool,SDL_SCANCODE_COUNT> keys{};
    std::array<bool,SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    Sint16 leftX=0,leftY=0,rightX=0,rightY=0,leftTrigger=0,rightTrigger=0;
    SDL_JoystickID gamepad=0;
    bool focused=true;
};
struct ControlFrame {
    Vec2 movement{},facing{};
    std::array<bool,ControlActionCount> down{},edges{};
    std::int8_t nativeTurnX=0;
    bool held(ControlAction action) const { return unsigned(action)<ControlActionCount&&down[unsigned(action)]; }
    bool pressed(ControlAction action) const { return unsigned(action)<ControlActionCount&&edges[unsigned(action)]; }
};

class ControlInput {
    std::array<bool,SDL_SCANCODE_COUNT> keyPulses{};
    std::array<bool,SDL_GAMEPAD_BUTTON_COUNT> padPulses{};
    std::array<bool,SDL_GAMEPAD_AXIS_COUNT> axisPulses{},axisEventDown{};
    std::array<bool,ControlActionCount> previousKey{},previousPad{};
    const ControlSettings* lastSettings=nullptr;
    std::uint64_t revision=0;
    SDL_JoystickID lastPad=0;
    bool seed=true,focusedForEvents=true,aimCancelled=false;
    void clearPulses() { keyPulses.fill(false); padPulses.fill(false);axisPulses.fill(false); }
    static float axis(Sint16 value) { return value>=0?float(value)/32767.0f:float(value)/32768.0f; }
public:
    void reset() { seed=true; aimCancelled=false;axisEventDown.fill(false);clearPulses(); }
    void cancelAimUntilRelease() { aimCancelled=true; }
    void onEvent(const SDL_Event& event) {
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST||event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {
            focusedForEvents=event.type==SDL_EVENT_WINDOW_FOCUS_GAINED; reset(); return;
        }
        if(!focusedForEvents) return;
        if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode>SDL_SCANCODE_UNKNOWN&&event.key.scancode<SDL_SCANCODE_COUNT)
            keyPulses[event.key.scancode]=true;
        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&event.gbutton.which==lastPad&&event.gbutton.button<SDL_GAMEPAD_BUTTON_COUNT)
            padPulses[event.gbutton.button]=true;
        if(event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&event.gaxis.which==lastPad&&
           (event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER||event.gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
            axisPulses[event.gaxis.axis]|=pressedEdge(event.gaxis.value>16383,axisEventDown[event.gaxis.axis]);
    }
    ControlFrame update(const ControlSettings& settings,const ControlRawState& raw) {
        ControlFrame result;
        const bool reseed=seed||!focusedForEvents||lastSettings!=&settings||revision!=settings.revision();
        const bool padChanged=lastPad!=raw.gamepad;
        lastSettings=&settings; revision=settings.revision(); lastPad=raw.gamepad; focusedForEvents=raw.focused;
        if(!raw.focused) { seed=true; aimCancelled=false; previousKey.fill(false); previousPad.fill(false);axisEventDown.fill(false);clearPulses(); return result; }
        result.nativeTurnX=raw.gamepad?std::int8_t(raw.leftX>>8):0;
        if(reseed||padChanged)axisPulses.fill(false);
        std::array<bool,ControlActionCount> key{},pad{},keyPulse{},padPulse{};
        for(unsigned i=0;i<ControlActionCount;++i) {
            const auto& binding=settings.binding(ControlAction(i));
            for(auto value:binding.keys) if(value!=SDL_SCANCODE_UNKNOWN) { key[i]=key[i]||raw.keys[value]; keyPulse[i]=keyPulse[i]||keyPulses[value]; }
            if(raw.gamepad) {
                for(auto value:binding.buttons)if(value!=SDL_GAMEPAD_BUTTON_INVALID){pad[i]=pad[i]||raw.buttons[value];padPulse[i]=padPulse[i]||padPulses[value];}
                for(auto value:binding.axes)if(value!=SDL_GAMEPAD_AXIS_INVALID){
                    pad[i]=pad[i]||(value==SDL_GAMEPAD_AXIS_LEFT_TRIGGER?raw.leftTrigger:raw.rightTrigger)>16383;
                    padPulse[i]=padPulse[i]||axisPulses[value];
                }
            }
        }
        Vec2 analog=raw.gamepad?radialStick(axis(raw.leftX),axis(raw.leftY),settings.deadzone()):Vec2{};
        auto digital=[](const auto& values) { return radialStick(float(values[unsigned(ControlAction::MoveRight)])-float(values[unsigned(ControlAction::MoveLeft)]),
            float(values[unsigned(ControlAction::MoveDown)])-float(values[unsigned(ControlAction::MoveUp)]),0); };
        const Vec2 padDigital=digital(pad); if(length(padDigital)>length(analog)) analog=padDigital;
        result.movement=digital(key); if(length(analog)>length(result.movement)) result.movement=analog;
        if(raw.gamepad)result.facing=radialStick(axis(raw.rightX),axis(raw.rightY),settings.deadzone());
        pad[unsigned(ControlAction::MoveUp)]=analog.z>0.001f; pad[unsigned(ControlAction::MoveDown)]=analog.z<-0.001f;
        pad[unsigned(ControlAction::MoveLeft)]=analog.x<-0.001f; pad[unsigned(ControlAction::MoveRight)]=analog.x>0.001f;
        for(unsigned i=0;i<ControlActionCount;++i) {
            result.down[i]=key[i]||pad[i];
            const bool keyboardEdge=pressedEdge(key[i],previousKey[i]),gamepadEdge=pressedEdge(pad[i],previousPad[i]);
            result.edges[i]=!reseed&&(keyboardEdge||keyPulse[i]||(!padChanged&&(gamepadEdge||padPulse[i])));
        }
        // Original Use lowers Aim; wait for physical release before reconciling held intent again.
        if(reseed||padChanged||!result.held(ControlAction::Aim))aimCancelled=false;
        if(aimCancelled){result.down[unsigned(ControlAction::Aim)]=false;result.edges[unsigned(ControlAction::Aim)]=false;}
        axisEventDown[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]=raw.gamepad&&raw.leftTrigger>16383;
        axisEventDown[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]=raw.gamepad&&raw.rightTrigger>16383;
        seed=false; clearPulses(); return result;
    }
    ControlFrame poll(const ControlSettings& settings,SDL_Gamepad* gamepad,bool focused=true) {
        ControlRawState raw; raw.focused=focused;
        int count=0; const bool* keys=SDL_GetKeyboardState(&count);
        for(int i=0;i<std::min(count,int(SDL_SCANCODE_COUNT));++i) raw.keys[i]=keys[i];
        if(gamepad&&SDL_GamepadConnected(gamepad)) {
            raw.gamepad=SDL_GetGamepadID(gamepad);
            for(unsigned i=0;i<SDL_GAMEPAD_BUTTON_COUNT;++i) raw.buttons[i]=SDL_GetGamepadButton(gamepad,SDL_GamepadButton(i));
            raw.leftX=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX); raw.leftY=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTY);
            raw.rightX=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHTX); raw.rightY=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHTY);
            raw.leftTrigger=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER); raw.rightTrigger=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        }
        return update(settings,raw);
    }
};
