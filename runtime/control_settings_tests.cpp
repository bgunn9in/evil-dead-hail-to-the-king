#include "control_settings.h"
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>

namespace fs=std::filesystem;
template<class F> bool rejects(F action) { try { action(); return false; } catch(const std::exception&) { return true; } }
SDL_Event keyEvent(SDL_Scancode key,bool repeat=false) {
    SDL_Event event{}; event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=key; event.key.repeat=repeat; return event;
}
static_assert(unsigned(ControlAction::QuickLoad)==13&&unsigned(ControlAction::ChainsawRev)==14&&unsigned(ControlAction::ChainsawStop)==15&&unsigned(ControlAction::Taunt)==16);
static_assert(unsigned(ControlAction::Forward)==17&&unsigned(ControlAction::Backward)==18&&unsigned(ControlAction::TurnLeft)==19&&unsigned(ControlAction::TurnRight)==20&&ControlActionCount==21);
std::array<ControlBinding,17> prefixKeys() {
    std::array<ControlBinding,17> data{};
    const std::array<SDL_Scancode,2> keys[]={
        {SDL_SCANCODE_W,SDL_SCANCODE_UP},{SDL_SCANCODE_S,SDL_SCANCODE_DOWN},{SDL_SCANCODE_A,SDL_SCANCODE_LEFT},{SDL_SCANCODE_D,SDL_SCANCODE_RIGHT},
        {SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT},{SDL_SCANCODE_SPACE,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_E,SDL_SCANCODE_RETURN},{SDL_SCANCODE_I,SDL_SCANCODE_TAB},
        {SDL_SCANCODE_ESCAPE,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_J,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_K,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_F11,SDL_SCANCODE_UNKNOWN},
        {SDL_SCANCODE_F5,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_F9,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_R,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_F,SDL_SCANCODE_UNKNOWN},{SDL_SCANCODE_T,SDL_SCANCODE_UNKNOWN}};
    for(unsigned n=0;n<17;++n)data[n].keys=keys[n];
    return data;
}
savefile::Bytes legacy(unsigned count=14,bool customPad=false,bool customKeys=false) {
    auto data=prefixKeys();
    const SDL_GamepadButton direction[]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    for(unsigned n=0;n<4;++n)data[n].buttons[0]=direction[n];
    data[6].buttons[0]=SDL_GAMEPAD_BUTTON_SOUTH;data[7].buttons={SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_BACK};
    data[8].buttons[0]=SDL_GAMEPAD_BUTTON_START;data[9].buttons[0]=SDL_GAMEPAD_BUTTON_WEST;data[10].buttons[0]=SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    data[14].buttons[0]=SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;data[15].buttons[0]=SDL_GAMEPAD_BUTTON_EAST;data[16].buttons[0]=SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if(customPad){data[5].buttons={SDL_GAMEPAD_BUTTON_LEFT_STICK,SDL_GAMEPAD_BUTTON_MISC1};data[4].buttons={SDL_GAMEPAD_BUTTON_MISC2,SDL_GAMEPAD_BUTTON_MISC3};}
    if(customKeys){data[6].keys[0]=SDL_SCANCODE_Q;data[7].keys[1]=SDL_SCANCODE_G;}
    statebytes::Writer out;out.text("EDCONTROL1");out.put<uint32_t>(1);out.put<uint32_t>(count);out.put(customKeys?.31f:.18f);
    for(unsigned n=0;n<count;++n){for(auto key:data[n].keys)out.put<int32_t>(key);for(auto button:data[n].buttons)out.put<int32_t>(button);}
    return std::move(out.bytes);
}
savefile::Bytes previous17(bool customPad=false,bool customKeys=false) {
    auto data=prefixKeys();
    const SDL_GamepadButton direction[]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    for(unsigned n=0;n<4;++n)data[n].buttons[0]=direction[n];
    data[4].buttons[0]=SDL_GAMEPAD_BUTTON_LEFT_STICK;data[5].buttons[0]=SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    data[6].buttons[0]=SDL_GAMEPAD_BUTTON_EAST;data[7].buttons[0]=SDL_GAMEPAD_BUTTON_START;data[8].buttons[0]=SDL_GAMEPAD_BUTTON_BACK;
    data[9].buttons[0]=SDL_GAMEPAD_BUTTON_SOUTH;data[10].buttons={SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
    data[15].buttons[0]=SDL_GAMEPAD_BUTTON_RIGHT_STICK;data[16].buttons[0]=SDL_GAMEPAD_BUTTON_NORTH;
    data[4].axes[0]=SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;data[14].axes[0]=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    if(customPad){data[6].buttons[0]=SDL_GAMEPAD_BUTTON_MISC1;data[6].axes[1]=SDL_GAMEPAD_AXIS_LEFT_TRIGGER;data[14].axes[0]=SDL_GAMEPAD_AXIS_INVALID;}
    if(customKeys){data[6].keys[0]=SDL_SCANCODE_Z;data[7].keys[1]=SDL_SCANCODE_G;}
    statebytes::Writer out;out.text("EDCONTROL1");out.put<uint32_t>(2);out.put<uint32_t>(17);out.put(customKeys?.31f:.18f);
    for(const auto& item:data){for(auto key:item.keys)out.put<int32_t>(key);for(auto button:item.buttons)out.put<int32_t>(button);for(auto axis:item.axes)out.put<int32_t>(axis);}
    return std::move(out.bytes);
}
savefile::Bytes legacy14(const ControlSettings&){return legacy();}
savefile::Bytes rawFile(const fs::path& path) {std::ifstream input(path,std::ios::binary);return savefile::Bytes(std::istreambuf_iterator<char>(input),{});}
SDL_Event axisEvent(SDL_GamepadAxis axis,Sint16 value){SDL_Event e{};e.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;e.gaxis.axis=Uint8(axis);e.gaxis.value=value;return e;}
void independentResetTests() {
    ControlSettings settings,defaults;
    assert(settings.bindKey(ControlAction::Use,0,SDL_SCANCODE_G)==BindingResult::Applied);
    assert(settings.bindButton(ControlAction::Use,0,SDL_GAMEPAD_BUTTON_MISC1)==BindingResult::Applied);
    assert(settings.bindAxis(ControlAction::ChainsawRev,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);
    assert(settings.bindAxis(ControlAction::Aim,0,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)==BindingResult::Applied);
    assert(settings.setDeadzone(.31f));const auto revision=settings.revision();
    settings.resetKeyboardDefaults();assert(settings.revision()!=revision&&settings.deadzone()==.31f);
    for(unsigned n=0;n<ControlActionCount;++n)assert(settings.binding(ControlAction(n)).keys==defaults.binding(ControlAction(n)).keys);
    assert(settings.binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_MISC1);
    assert(settings.binding(ControlAction::Aim).axes[0]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    assert(settings.bindKey(ControlAction::Use,0,SDL_SCANCODE_G)==BindingResult::Applied);
    settings.resetGamepadDefaults();assert(settings.binding(ControlAction::Use).keys[0]==SDL_SCANCODE_G);
    assert(settings.deadzone()==defaults.deadzone());
    for(unsigned n=0;n<ControlActionCount;++n){const auto action=ControlAction(n);
        assert(settings.binding(action).buttons==defaults.binding(action).buttons&&settings.binding(action).axes==defaults.binding(action).axes);}
    ControlSettings restored;restored.restore(settings.serialize());assert(restored.serialize()==settings.serialize());
}
void modernKeyboardAndLookTests() {
    ControlSettings settings;ControlInput input;ControlRawState raw;raw.gamepad=42;input.update(settings,raw);
    raw.keys[SDL_SCANCODE_Q]=true;auto frame=input.update(settings,raw);assert(frame.pressed(ControlAction::PrimaryAttack)&&length(frame.movement)==0);
    raw.keys[SDL_SCANCODE_Q]=false;raw.keys[SDL_SCANCODE_C]=true;assert(input.update(settings,raw).pressed(ControlAction::SecondaryAttack));
    raw.keys[SDL_SCANCODE_C]=false;raw.keys[SDL_SCANCODE_LCTRL]=true;assert(input.update(settings,raw).pressed(ControlAction::Backward));raw.keys[SDL_SCANCODE_LCTRL]=false;
    raw.rightX=32767;frame=input.update(settings,raw);assert(frame.facing.x>.99f&&length(frame.movement)==0&&frame.nativeTurnX==0);
    raw.rightX=3000;raw.rightY=-3000;assert(length(input.update(settings,raw).facing)==0);
    raw.rightX=-32768;raw.rightY=0;assert(input.update(settings,raw).facing.x<-.99f);
    raw.focused=false;assert(length(input.update(settings,raw).facing)==0);raw.focused=true;raw.gamepad=0;assert(length(input.update(settings,raw).facing)==0);
    for(bool custom:{false,true}){
        statebytes::Writer old;old.text("EDCONTROL1");old.put<uint32_t>(2);old.put<uint32_t>(ControlActionCount);old.put(settings.deadzone());
        for(unsigned n=0;n<ControlActionCount;++n){auto binding=settings.binding(ControlAction(n));
            if(n==unsigned(ControlAction::PrimaryAttack)||n==unsigned(ControlAction::SecondaryAttack)||n==unsigned(ControlAction::Backward))binding.keys[1]=SDL_SCANCODE_UNKNOWN;
            if(custom&&n==unsigned(ControlAction::Use))binding.keys[0]=SDL_SCANCODE_Q;
            for(auto key:binding.keys)old.put<int32_t>(key);for(auto button:binding.buttons)old.put<int32_t>(button);for(auto axis:binding.axes)old.put<int32_t>(axis);
        }
        ControlSettings migrated;migrated.restore(old.bytes);
        assert(migrated.binding(ControlAction::PrimaryAttack).keys[1]==(custom?SDL_SCANCODE_UNKNOWN:SDL_SCANCODE_Q));
        assert(migrated.binding(ControlAction::Use).keys[0]==(custom?SDL_SCANCODE_Q:SDL_SCANCODE_E));
        ControlSettings loaded;loaded.restore(migrated.serialize());assert(loaded.serialize()==migrated.serialize());
    }
    assert(settings.bindKey(ControlAction::PrimaryAttack,1,SDL_SCANCODE_UNKNOWN)==BindingResult::Applied);
    ControlSettings loaded;loaded.restore(settings.serialize());assert(loaded.binding(ControlAction::PrimaryAttack).keys[1]==SDL_SCANCODE_UNKNOWN);
}
void motorAndMigrationTests() {
    ControlSettings defaults;
    assert(defaults.binding(ControlAction::PrimaryAttack).buttons[0]==SDL_GAMEPAD_BUTTON_SOUTH);
    assert(defaults.binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_EAST);
    assert((defaults.binding(ControlAction::SecondaryAttack).buttons==std::array<SDL_GamepadButton,2>{SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_INVALID}));
    assert(defaults.binding(ControlAction::Inventory).buttons[0]==SDL_GAMEPAD_BUTTON_START);
    assert(defaults.binding(ControlAction::Pause).buttons[0]==SDL_GAMEPAD_BUTTON_BACK);
    assert(defaults.binding(ControlAction::Taunt).buttons[0]==SDL_GAMEPAD_BUTTON_NORTH);
    assert(defaults.binding(ControlAction::ChainsawStop).buttons[0]==SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    assert(defaults.binding(ControlAction::ChainsawRev).axes[0]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    assert(defaults.binding(ControlAction::Run).axes[0]==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    assert(defaults.binding(ControlAction::Aim).buttons[0]==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    for(unsigned count:{14u,16u,17u})for(bool customKeys:{false,true}){
        ControlSettings migrated;migrated.restore(legacy(count,false,customKeys));
        for(unsigned n=0;n<ControlActionCount;++n){assert(migrated.binding(ControlAction(n)).buttons==defaults.binding(ControlAction(n)).buttons);assert(migrated.binding(ControlAction(n)).axes==defaults.binding(ControlAction(n)).axes);}
        assert(migrated.binding(ControlAction::Use).keys[0]==(customKeys?SDL_SCANCODE_Q:SDL_SCANCODE_E));
        assert(migrated.deadzone()==(customKeys?.31f:.18f));
        migrated.restore(legacy(count,true,customKeys));
        assert((migrated.binding(ControlAction::Aim).buttons==std::array<SDL_GamepadButton,2>{SDL_GAMEPAD_BUTTON_LEFT_STICK,SDL_GAMEPAD_BUTTON_MISC1}));
        assert((migrated.binding(ControlAction::Run).buttons==std::array<SDL_GamepadButton,2>{SDL_GAMEPAD_BUTTON_MISC2,SDL_GAMEPAD_BUTTON_MISC3}));
        assert(migrated.binding(ControlAction::Aim).axes[0]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER&&migrated.binding(ControlAction::Run).axes[0]==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        assert(migrated.binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_SOUTH);
        assert(migrated.binding(ControlAction::Use).keys[0]==(customKeys?SDL_SCANCODE_Q:SDL_SCANCODE_E));
    }
    ControlSettings migrated;auto old=legacy(14,true);const int32_t oldCustomR=SDL_SCANCODE_R;std::memcpy(old.data()+26+6*16,&oldCustomR,4);
    const int32_t oldCustomLB=SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;std::memcpy(old.data()+26+5*16+8,&oldCustomLB,4);
    migrated.restore(old);assert(migrated.binding(ControlAction::Use).keys[0]==SDL_SCANCODE_R&&migrated.binding(ControlAction::ChainsawRev).keys[0]==SDL_SCANCODE_UNKNOWN);
    assert(migrated.binding(ControlAction::Aim).buttons[0]==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER&&migrated.binding(ControlAction::ChainsawRev).buttons[0]==SDL_GAMEPAD_BUTTON_INVALID);
    auto changed=defaults;const auto safe=changed.serialize();const auto revision=changed.revision();
    assert(changed.bindAxis(ControlAction::Use,0,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)==BindingResult::Conflict);
    assert(changed.bindAxis(ControlAction::QuickSave,0,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)==BindingResult::Invalid);
    assert(changed.bindAxis(ControlAction::Use,0,SDL_GAMEPAD_AXIS_LEFTX)==BindingResult::Invalid);
    assert(changed.serialize()==safe&&changed.revision()==revision);
    assert(changed.bindAxis(ControlAction::ChainsawRev,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);
    assert(changed.capture(ControlAction::Use,1,axisEvent(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,16383))==BindingResult::Ignored);
    assert(changed.capture(ControlAction::Use,1,axisEvent(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,20000))==BindingResult::Applied);
    assert(changed.binding(ControlAction::Use).buttons[0]==SDL_GAMEPAD_BUTTON_EAST&&changed.binding(ControlAction::Use).axes[1]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    assert(changed.bindButton(ControlAction::Use,1,SDL_GAMEPAD_BUTTON_MISC1)==BindingResult::Applied);
    assert(changed.binding(ControlAction::Use).axes[1]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    ControlSettings roundtrip;roundtrip.restore(changed.serialize());assert(roundtrip.serialize()==changed.serialize());
    auto retained=[&](const savefile::Bytes& bytes){const auto bytesBefore=roundtrip.serialize();const auto rev=roundtrip.revision();assert(rejects([&]{roundtrip.restore(bytes);}));assert(roundtrip.serialize()==bytesBefore&&roundtrip.revision()==rev);};
    for(unsigned count:{0u,13u,15u,18u,21u}){auto bytes=legacy();std::memcpy(bytes.data()+18,&count,4);retained(bytes);}
    for(unsigned count:{14u,16u,18u}){auto bytes=safe;std::memcpy(bytes.data()+18,&count,4);retained(bytes);}
    for(size_t n=0;n<safe.size();++n)retained(savefile::Bytes(safe.begin(),safe.begin()+n));
    auto bytes=safe;bytes.push_back(0);retained(bytes);
    const int32_t badAxis=SDL_GAMEPAD_AXIS_LEFTX;bytes=safe;std::memcpy(bytes.data()+26+6*24+16,&badAxis,4);retained(bytes);
    const int32_t duplicateAxis=SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;bytes=safe;std::memcpy(bytes.data()+26+6*24+16,&duplicateAxis,4);retained(bytes);
    bytes=legacy();const int32_t duplicateButton=SDL_GAMEPAD_BUTTON_SOUTH;std::memcpy(bytes.data()+26+5*16+8,&duplicateButton,4);retained(bytes);
    ControlInput input;ControlRawState raw;raw.gamepad=5;input.update(defaults,raw);raw.leftTrigger=20000;
    auto frame=input.update(defaults,raw);assert(frame.held(ControlAction::ChainsawRev)&&frame.pressed(ControlAction::ChainsawRev)&&!frame.held(ControlAction::Aim));
    assert(!input.update(defaults,raw).pressed(ControlAction::ChainsawRev));raw.gamepad=6;assert(!input.update(defaults,raw).pressed(ControlAction::ChainsawRev));
    raw.focused=false;input.update(defaults,raw);raw.focused=true;assert(!input.update(defaults,raw).pressed(ControlAction::ChainsawRev));
    raw.leftTrigger=0;input.update(defaults,raw);raw.leftTrigger=20000;assert(input.update(defaults,raw).pressed(ControlAction::ChainsawRev));
    auto remapped=defaults;assert(remapped.bindAxis(ControlAction::ChainsawRev,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);
    assert(remapped.bindAxis(ControlAction::Use,0,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)==BindingResult::Applied);
    frame=input.update(remapped,raw);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use)&&!frame.held(ControlAction::ChainsawRev));
}
void canonicalTests() {
    ControlSettings defaults;
    const ControlAction actions[]={ControlAction::Forward,ControlAction::Backward,ControlAction::TurnLeft,ControlAction::TurnRight};
    const char* names[]={"forward","backward","turn left","turn right"};
    const SDL_GamepadButton buttons[]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    for(unsigned n=0;n<4;++n){assert(std::string(controlActionName(actions[n]))==names[n]);assert(defaults.binding(actions[n]).buttons[0]==buttons[n]);
        for(auto button:defaults.binding(ControlAction(n)).buttons)assert(button==SDL_GAMEPAD_BUTTON_INVALID);}
    assert(defaults.binding(ControlAction::Backward).keys[0]==SDL_SCANCODE_Z&&defaults.binding(ControlAction::Backward).buttons[1]==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    for(bool customKeys:{false,true}){
        ControlSettings migrated;migrated.restore(previous17(false,customKeys));
        for(unsigned n=0;n<ControlActionCount;++n){const auto& actual=migrated.binding(ControlAction(n));const auto& expected=defaults.binding(ControlAction(n));assert(actual.buttons==expected.buttons&&actual.axes==expected.axes);}
        assert(migrated.binding(ControlAction::Use).keys[0]==(customKeys?SDL_SCANCODE_Z:SDL_SCANCODE_E));
        assert(migrated.binding(ControlAction::Backward).keys[0]==(customKeys?SDL_SCANCODE_UNKNOWN:SDL_SCANCODE_Z));
        assert(migrated.deadzone()==(customKeys?.31f:.18f));
        const auto old=previous17(true,customKeys);migrated.restore(old);const auto restored=migrated.serialize();
        for(unsigned n=0;n<17;++n){
            if(customKeys)assert(std::equal(old.begin()+26+n*24,old.begin()+26+n*24+8,restored.begin()+26+n*24));
            assert(std::equal(old.begin()+26+n*24+8,old.begin()+26+(n+1)*24,restored.begin()+26+n*24+8));
        }
        for(auto action:actions)assert(migrated.binding(action).buttons[0]==SDL_GAMEPAD_BUTTON_INVALID);
        assert(migrated.binding(ControlAction::Backward).buttons[1]==SDL_GAMEPAD_BUTTON_INVALID);
        assert(migrated.binding(ControlAction::Use).axes[1]==SDL_GAMEPAD_AXIS_LEFT_TRIGGER&&migrated.binding(ControlAction::ChainsawRev).axes[0]==SDL_GAMEPAD_AXIS_INVALID);
        ControlSettings roundtrip;roundtrip.restore(restored);assert(roundtrip.serialize()==restored);
    }
    // A single changed pad slot keeps the complete old mapping, including its aliases.
    auto alias=previous17();const int32_t misc=SDL_GAMEPAD_BUTTON_MISC1;std::memcpy(alias.data()+26+6*24+8,&misc,4);
    ControlSettings retained;retained.restore(alias);assert(retained.binding(ControlAction::MoveUp).buttons[0]==SDL_GAMEPAD_BUTTON_DPAD_UP);
    assert(retained.binding(ControlAction::SecondaryAttack).buttons[1]==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER&&retained.binding(ControlAction::Backward).buttons[1]==SDL_GAMEPAD_BUTTON_INVALID);
    ControlInput input;ControlRawState raw;raw.gamepad=99;input.update(defaults,raw);
    for(unsigned n=0;n<4;++n){raw.buttons[buttons[n]]=true;auto frame=input.update(defaults,raw);assert(frame.held(actions[n])&&frame.pressed(actions[n])&&length(frame.movement)==0);
        for(unsigned other=0;other<4;++other)assert(frame.held(actions[other])==(other==n));
        assert(!frame.held(ControlAction::Run)&&!frame.held(ControlAction::SecondaryAttack));raw.buttons[buttons[n]]=false;input.update(defaults,raw);}
    raw.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER]=true;auto frame=input.update(defaults,raw);assert(frame.held(ControlAction::Backward)&&!frame.held(ControlAction::SecondaryAttack)&&length(frame.movement)==0);
    raw.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER]=false;raw.keys[SDL_SCANCODE_Z]=true;assert(input.update(defaults,raw).held(ControlAction::Backward));raw.keys[SDL_SCANCODE_Z]=false;
    raw.keys[SDL_SCANCODE_W]=true;frame=input.update(defaults,raw);assert(length(frame.movement)>.99f&&!frame.held(ControlAction::Forward));raw.keys[SDL_SCANCODE_W]=false;
    raw.leftX=32767;frame=input.update(defaults,raw);assert(frame.movement.x>.99f&&!frame.held(ControlAction::TurnRight));
    for(auto action:actions){auto remapped=defaults;assert(remapped.bindKey(action,1,SDL_SCANCODE_G)==BindingResult::Applied);
        assert(remapped.bindButton(action,action==ControlAction::Backward?0:1,SDL_GAMEPAD_BUTTON_MISC1)==BindingResult::Applied);
        assert(remapped.bindAxis(ControlAction::ChainsawRev,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);assert(remapped.bindAxis(action,1,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)==BindingResult::Applied);}
}
void tauntMigrationTests() {assert(std::string(controlActionName(ControlAction::Taunt))=="taunt");}
void nativeTurnTransportTests() {
    ControlSettings settings;assert(settings.setDeadzone(.5f));ControlInput input;ControlRawState raw;raw.gamepad=7;
    for(const auto pair:std::array<std::pair<Sint16,int>,7>{{{32767,127},{-32768,-128},{255,0},{256,1},{-1,-1},{-256,-1},{-257,-2}}}){
        raw.leftX=pair.first;assert(input.update(settings,raw).nativeTurnX==pair.second);
    }
    raw.leftX=6000;const auto frame=input.update(settings,raw);assert(length(frame.movement)==0&&frame.nativeTurnX==23);
    raw.focused=false;assert(input.update(settings,raw).nativeTurnX==0);raw.focused=true;raw.gamepad=0;assert(input.update(settings,raw).nativeTurnX==0);
}
void aimCancelTests() {
    ControlSettings settings;ControlInput input;ControlRawState raw;raw.gamepad=10;input.update(settings,raw);
    raw.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]=true;auto frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&frame.pressed(ControlAction::Aim));
    input.cancelAimUntilRelease();
    for(unsigned n=0;n<3;++n){frame=input.update(settings,raw);assert(!frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));}
    raw.keys[SDL_SCANCODE_SPACE]=true;raw.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]=false;assert(!input.update(settings,raw).held(ControlAction::Aim));
    raw.keys[SDL_SCANCODE_SPACE]=false;assert(!input.update(settings,raw).held(ControlAction::Aim));
    raw.keys[SDL_SCANCODE_SPACE]=true;frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&frame.pressed(ControlAction::Aim));
    input.cancelAimUntilRelease();input.reset();frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    input.cancelAimUntilRelease();raw.focused=false;assert(!input.update(settings,raw).held(ControlAction::Aim));raw.focused=true;
    frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    input.cancelAimUntilRelease();SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;input.onEvent(focus);focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;input.onEvent(focus);
    frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    input.cancelAimUntilRelease();raw.gamepad=11;assert(input.update(settings,raw).held(ControlAction::Aim));
    input.cancelAimUntilRelease();assert(settings.setDeadzone(.2f));frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    input.cancelAimUntilRelease();auto replaced=settings;assert(input.update(replaced,raw).held(ControlAction::Aim));
    raw.keys[SDL_SCANCODE_SPACE]=false;assert(settings.bindKey(ControlAction::Aim,0,SDL_SCANCODE_G)==BindingResult::Applied);input.update(settings,raw);
    raw.keys[SDL_SCANCODE_G]=true;assert(input.update(settings,raw).held(ControlAction::Aim));input.cancelAimUntilRelease();
    raw.keys[SDL_SCANCODE_SPACE]=true;frame=input.update(settings,raw);assert(!frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    raw.keys[SDL_SCANCODE_G]=false;input.update(settings,raw);raw.keys[SDL_SCANCODE_G]=true;
    frame=input.update(settings,raw);assert(frame.held(ControlAction::Aim)&&frame.pressed(ControlAction::Aim));
}
void triggerPulseTests() {
    for(auto axis:{SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
        ControlSettings settings;assert(settings.bindAxis(ControlAction::ChainsawRev,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);
        assert(settings.bindAxis(ControlAction::Run,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);
        assert(settings.bindAxis(ControlAction::Use,0,axis)==BindingResult::Applied);
        ControlInput input;ControlRawState raw;raw.gamepad=77;input.update(settings,raw);
        auto motion=[&](Sint16 value,SDL_JoystickID id=77){auto e=axisEvent(axis,value);e.gaxis.which=id;input.onEvent(e);};
        auto set=[&](Sint16 value){(axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER?raw.leftTrigger:raw.rightTrigger)=value;};
        motion(20000);motion(0);auto frame=input.update(settings,raw);
        assert(frame.pressed(ControlAction::Use)&&!frame.held(ControlAction::Use));assert(!input.update(settings,raw).pressed(ControlAction::Use));
        motion(20000,78);motion(0,78);assert(!input.update(settings,raw).pressed(ControlAction::Use));
        motion(20000);set(20000);assert(input.update(settings,raw).pressed(ControlAction::Use));
        for(Sint16 value:{Sint16(21000),Sint16(30000),Sint16(17000)}){motion(value);set(value);frame=input.update(settings,raw);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));}
        motion(16383);set(16383);assert(!input.update(settings,raw).held(ControlAction::Use));motion(16384);set(16384);assert(input.update(settings,raw).pressed(ControlAction::Use));
        input.reset();motion(20000);set(20000);frame=input.update(settings,raw);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));
        motion(22000);set(22000);assert(!input.update(settings,raw).pressed(ControlAction::Use));
        SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;input.onEvent(focus);raw.focused=false;assert(!input.update(settings,raw).held(ControlAction::Use));
        focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;input.onEvent(focus);raw.focused=true;frame=input.update(settings,raw);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));
        motion(0);set(0);input.update(settings,raw);motion(20000);motion(0);assert(settings.setDeadzone(.2f));assert(!input.update(settings,raw).pressed(ControlAction::Use));
        motion(20000);set(20000);raw.gamepad=78;frame=input.update(settings,raw);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));
    }
}
void virtualTriggerPulseTests(SDL_Gamepad* gamepad,SDL_Joystick* joystick) {
    ControlSettings settings;assert(settings.bindAxis(ControlAction::ChainsawRev,0,SDL_GAMEPAD_AXIS_INVALID)==BindingResult::Applied);
    assert(settings.bindAxis(ControlAction::Use,0,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)==BindingResult::Applied);
    ControlInput input;SDL_Event event;
    auto pump=[&]{while(SDL_PollEvent(&event))input.onEvent(event);};
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,false));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MIN));SDL_UpdateGamepads();pump();input.poll(settings,gamepad);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767));SDL_UpdateGamepads();pump();
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MIN));SDL_UpdateGamepads();pump();
    auto frame=input.poll(settings,gamepad);assert(frame.pressed(ControlAction::Use)&&!frame.held(ControlAction::Use));assert(!input.poll(settings,gamepad).pressed(ControlAction::Use));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767));SDL_UpdateGamepads();pump();assert(input.poll(settings,gamepad).pressed(ControlAction::Use));
    for(Sint16 value:{Sint16(25000),Sint16(30000),Sint16(24000)}){assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,value));SDL_UpdateGamepads();pump();frame=input.poll(settings,gamepad);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));}
    input.reset();pump();frame=input.poll(settings,gamepad);assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MIN));SDL_UpdateGamepads();pump();input.poll(settings,gamepad);
}
int main() {
    independentResetTests();
    modernKeyboardAndLookTests();
    motorAndMigrationTests();
    canonicalTests();
    nativeTurnTransportTests();
    tauntMigrationTests();
    aimCancelTests();
    triggerPulseTests();
    ControlSettings settings; const auto defaults=settings.serialize();
    assert(settings.binding(ControlAction::Aim).keys[0]==SDL_SCANCODE_SPACE);
    assert(settings.bindKey(ControlAction::Use,0,SDL_SCANCODE_W)==BindingResult::Conflict);
    assert(settings.bindButton(ControlAction::Use,0,SDL_GAMEPAD_BUTTON_WEST)==BindingResult::Conflict);
    assert(settings.bindButton(ControlAction::QuickSave,0,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)==BindingResult::Invalid);
    assert(settings.serialize()==defaults);
    const auto revision=settings.revision();
    assert(!settings.setDeadzone(-0.01f)&&!settings.setDeadzone(0.51f)&&!settings.setDeadzone(std::numeric_limits<float>::quiet_NaN()));
    assert(settings.revision()==revision&&settings.setDeadzone(0)&&settings.setDeadzone(0.5f)); settings.resetDefaults();
    assert(settings.capture(ControlAction::Use,0,keyEvent(SDL_SCANCODE_ESCAPE))==BindingResult::Cancelled);
    assert(settings.capture(ControlAction::Use,0,keyEvent(SDL_SCANCODE_UNKNOWN))==BindingResult::Invalid);
    assert(settings.capture(ControlAction::Use,0,keyEvent(SDL_SCANCODE_G,true))==BindingResult::Ignored);
    assert(settings.capture(ControlAction::Use,0,keyEvent(SDL_SCANCODE_G))==BindingResult::Applied);
    assert(settings.binding(ControlAction::Use).keys[0]==SDL_SCANCODE_G);
    auto saved=settings.serialize(); ControlSettings loaded; loaded.restore(saved); assert(loaded.serialize()==saved);
    for(unsigned fault=0;fault<4;++fault) {
        auto broken=saved;
        if(fault==0) broken.pop_back();
        if(fault==1) { const int32_t conflict=SDL_SCANCODE_W; std::memcpy(broken.data()+26+unsigned(ControlAction::Use)*24,&conflict,4); }
        if(fault==2) { const int32_t invalid=-7; std::memcpy(broken.data()+26,&invalid,4); }
        if(fault==3) { const float invalid=std::numeric_limits<float>::infinity(); std::memcpy(broken.data()+22,&invalid,4); }
        assert(rejects([&]{loaded.restore(broken);})); assert(loaded.serialize()==saved);
    }
    settings.resetDefaults();
    ControlInput controls; ControlRawState raw; raw.gamepad=10; raw.buttons[SDL_GAMEPAD_BUTTON_EAST]=true;
    auto frame=controls.update(settings,raw); assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));
    assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    raw.buttons[SDL_GAMEPAD_BUTTON_EAST]=false; controls.update(settings,raw);
    raw.buttons[SDL_GAMEPAD_BUTTON_EAST]=true; assert(controls.update(settings,raw).pressed(ControlAction::Use));
    raw.gamepad=11; assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    raw.gamepad=0; assert(!controls.update(settings,raw).held(ControlAction::Use));
    raw.gamepad=12; assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    raw.focused=false; controls.update(settings,raw); controls.onEvent(keyEvent(SDL_SCANCODE_E));
    raw.focused=true; assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    raw.buttons[SDL_GAMEPAD_BUTTON_EAST]=false; controls.update(settings,raw);
    controls.onEvent(keyEvent(SDL_SCANCODE_E)); frame=controls.update(settings,raw);
    assert(frame.pressed(ControlAction::Use)&&!frame.held(ControlAction::Use));
    assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    controls.onEvent(keyEvent(SDL_SCANCODE_E,true)); assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    assert(settings.bindKey(ControlAction::Use,0,SDL_SCANCODE_G)==BindingResult::Applied);
    raw.keys[SDL_SCANCODE_G]=true; assert(!controls.update(settings,raw).pressed(ControlAction::Use));
    raw.keys[SDL_SCANCODE_G]=false; controls.update(settings,raw);
    raw.keys[SDL_SCANCODE_G]=true; assert(controls.update(settings,raw).pressed(ControlAction::Use));
    raw.leftX=3000; raw.leftTrigger=raw.rightTrigger=20000;
    frame=controls.update(settings,raw); assert(length(frame.movement)==0&&frame.held(ControlAction::ChainsawRev)&&frame.held(ControlAction::Run));
    assert(settings.setDeadzone(0)); assert(length(controls.update(settings,raw).movement)>0);
    assert(settings.setDeadzone(0.5f)); raw.leftX=16383; assert(length(controls.update(settings,raw).movement)==0);

    const auto directory=fs::temp_directory_path()/("evil-dead-controls-test-"+std::to_string(std::random_device{}()));
    assert(fs::create_directory(directory)); assert(!loaded.load(directory));
    const auto oldFile=legacy14(ControlSettings{});savefile::atomicWrite(directory,"controls.eds",oldFile,4096);
    const auto oldEnvelope=rawFile(directory/"controls.eds");assert(loaded.load(directory)&&loaded.serialize()==defaults&&rawFile(directory/"controls.eds")==oldEnvelope);
    loaded.save(directory);assert(savefile::read(directory,"controls.eds",4096)==defaults);
    auto badOld=oldFile;const int32_t duplicate=SDL_SCANCODE_W;std::memcpy(badOld.data()+26+unsigned(ControlAction::Use)*16,&duplicate,4);
    savefile::atomicWrite(directory,"controls.eds",badOld,4096);const auto invalidEnvelope=rawFile(directory/"controls.eds");
    assert(rejects([&]{loaded.load(directory);}));assert(loaded.serialize()==defaults&&rawFile(directory/"controls.eds")==invalidEnvelope);
    settings.save(directory); assert(loaded.load(directory)&&loaded.serialize()==settings.serialize());
    const auto before=loaded.serialize();
    { std::fstream file(directory/"controls.eds",std::ios::in|std::ios::out|std::ios::binary);
      char bad=0; file.seekg(-1,std::ios::end); file.read(&bad,1); bad^=0x40; file.seekp(-1,std::ios::end); file.write(&bad,1); }
    assert(rejects([&]{loaded.load(directory);})); assert(loaded.serialize()==before);
    fs::remove(directory/"controls.eds"); assert(fs::remove(directory));

    assert(SDL_Init(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc descriptor{}; SDL_INIT_INTERFACE(&descriptor);
    descriptor.type=SDL_JOYSTICK_TYPE_GAMEPAD; descriptor.naxes=SDL_GAMEPAD_AXIS_COUNT; descriptor.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1; descriptor.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    descriptor.name="Evil Dead control settings proof";
    const auto id=SDL_AttachVirtualJoystick(&descriptor); assert(id);
    SDL_Gamepad* gamepad=SDL_OpenGamepad(id); assert(gamepad);
    SDL_Joystick* joystick=SDL_GetGamepadJoystick(gamepad);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,SDL_JOYSTICK_AXIS_MIN));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,SDL_JOYSTICK_AXIS_MIN));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true)); SDL_UpdateGamepads();
    settings.resetDefaults(); ControlInput live;
    frame=live.poll(settings,gamepad); assert(frame.held(ControlAction::Use)&&!frame.pressed(ControlAction::Use));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,false)); SDL_UpdateGamepads(); live.poll(settings,gamepad);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,32767));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767)); SDL_UpdateGamepads();
    frame=live.poll(settings,gamepad); assert(frame.pressed(ControlAction::Use)&&frame.held(ControlAction::ChainsawRev)&&frame.held(ControlAction::Run)&&frame.movement.x>0.99f);
    for(auto pair:{std::pair{ControlAction::ChainsawStop,SDL_GAMEPAD_BUTTON_RIGHT_STICK},std::pair{ControlAction::Taunt,SDL_GAMEPAD_BUTTON_NORTH}}) {
        assert(SDL_SetJoystickVirtualButton(joystick,pair.second,true));SDL_UpdateGamepads();frame=live.poll(settings,gamepad);
        assert(frame.pressed(pair.first)&&frame.held(pair.first));assert(!live.poll(settings,gamepad).pressed(pair.first));
        assert(SDL_SetJoystickVirtualButton(joystick,pair.second,false));SDL_UpdateGamepads();live.poll(settings,gamepad);
        assert(SDL_SetJoystickVirtualButton(joystick,pair.second,true));SDL_UpdateGamepads();assert(live.poll(settings,gamepad).pressed(pair.first));
        assert(SDL_SetJoystickVirtualButton(joystick,pair.second,false));SDL_UpdateGamepads();live.poll(settings,gamepad);
    }
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));SDL_UpdateGamepads();live.poll(settings,gamepad);
    live.cancelAimUntilRelease();frame=live.poll(settings,gamepad);assert(!frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false));SDL_UpdateGamepads();assert(!live.poll(settings,gamepad).held(ControlAction::Aim));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));SDL_UpdateGamepads();frame=live.poll(settings,gamepad);assert(frame.held(ControlAction::Aim)&&frame.pressed(ControlAction::Aim));
    live.cancelAimUntilRelease();live.reset();frame=live.poll(settings,gamepad);assert(frame.held(ControlAction::Aim)&&!frame.pressed(ControlAction::Aim));
    virtualTriggerPulseTests(gamepad,joystick);
    SDL_CloseGamepad(gamepad); assert(SDL_DetachVirtualJoystick(id)); SDL_UpdateGamepads();
    assert(!live.poll(settings,nullptr).held(ControlAction::Use)); SDL_Quit();
}
