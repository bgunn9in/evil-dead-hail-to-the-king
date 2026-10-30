#pragma once
#include "jpc.h"
#include "script_profile.h"
#include <array>

struct ScriptInventoryItem { int itemID=0,quantity=0,slot=0; };

struct ScriptGame {
    jpc::Package package;
    ScriptProfile profile;
    uint32_t hostField(HostFieldRole role) const {return profile.field(role);}
    jpc::Interpreter vm;
    jpc::Ref ash=0,scene=0,level=0,inventory=0;
    int mode=0;
    uint32_t messageAsset=0;
    std::string diagnostic;
    std::map<int,int> guiFlags;
    explicit ScriptGame(const std::filesystem::path& file,bool compatibilityFixture=true):package(jpc::Package::load(file)),profile(package),vm(package,1000000) {
        for(const auto& method:package.methods)if(method.name=="<clinit>")
            vm.initialize(package.classes[size_t(method.owner)].name);
        auto object=[&](const char* owner,jpc::Interpreter::Fields fields=jpc::Interpreter::Fields{}) {
            return vm.makeObject(package.classIndex(owner),std::move(fields));
        };
        const auto I=[](int32_t value){return jpc::Value::integer(value);};
        const auto R=[](jpc::Ref value){return jpc::Value::reference(value);};
        if(compatibilityFixture) {
        ash=object("Ash",{{5,I(0)},{6,I(1)},{7,I(-1)},{8,I(-1)},{13,I(100)},{95,I(100)}});
        level=object("LevelA");vm.statics[310]=R(ash);vm.statics[396]=R(level);
        // shortcut: source inventory constructors run now; full Ash.initialize also needs native weapon setup.
        for(const auto& setup:std::vector<std::tuple<int,int,int,int,const char*,int>>{
            {99,12,80,258,"InvItem",868},{98,5,2,256,"InvWeapon",867},
            {100,14,256,66,"InvChest",877},{101,14,80,0,"InvChest",878}}) {
            const auto [field,slots,accepted,excluded,guiClass,global]=setup;
            const auto ui=object(guiClass),container=object("Inventory");
            vm.statics[uint32_t(global)]=R(ui);
            vm.invoke(package.methodIndex("Inventory","<init>","(Led/base/EDNpc;III)V"),
                      container,{R(ash),I(slots),I(accepted),I(excluded)});
            vm.setField(container,hostField(HostFieldRole::InventoryGui),R(ui));vm.setField(ash,uint32_t(field),R(container));
            if(field==99)inventory=container;
        }
        const auto face=object("GameAshFace",{{379,R(ash)},{835,R(vm.makeArray('I',{I(11)}))},{836,I(7)}});
        const auto text=object("GameTextBox",{{835,R(vm.makeArray('I',{I(100)}))},{836,I(200)}});
        vm.statics[849]=R(face);vm.statics[851]=R(text);
        }
        auto bind=[&](const std::string& name,jpc::Interpreter::Native native) {
            const auto method=std::find_if(package.methods.begin(),package.methods.end(),[&](const auto& m){
                return package.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name;
            });
            if(method==package.methods.end())throw jpc::Error("Missing native bridge: "+name);
            vm.bind("ed/base/External",name,method->signature,std::move(native));
        };
        bind("gameStateChange",[this](auto&,auto,const auto& args){mode=args[0].integer();return jpc::Value{};});
        bind("guiSysAddFlag",[this](auto&,auto,const auto& args){guiFlags[args[0].integer()]|=args[1].integer();return jpc::Value{};});
        bind("guiSysRemoveFlag",[this](auto&,auto,const auto& args){guiFlags[args[0].integer()]&=~args[1].integer();return jpc::Value{};});
        bind("guiItemSetDataID",[this](auto&,auto,const auto& args){messageAsset=uint32_t(args[1].integer());return jpc::Value{};});
        bind("guiSysAtOffset",[](auto&,auto,const auto&){return jpc::Value::integer(1);});
        bind("guiItemSetMayaVal",[](auto&,auto,const auto&){return jpc::Value{};});
        bind("guiItemSetColor",[](auto&,auto,const auto&){return jpc::Value{};});
        bind("debugPrintString",[this](auto&,auto,const auto& args){diagnostic=string(args[0].reference());return jpc::Value{};});
        bind("debugPrintInt",[this](auto&,auto,const auto& args){diagnostic=std::to_string(args[0].integer());return jpc::Value{};});
    }
    // Native406F60/406FB0 share the persistent buffer43926E0. Its two header
    // words are regenerated on each call; only plain bytes survive package reset.
    struct PlayerTransferBuffer {
        static constexpr size_t NativeBytes=0x3840,HeaderBytes=8,PayloadBytes=NativeBytes-HeaderBytes;
        std::array<uint8_t,NativeBytes> bytes{};
        void clear(){bytes.fill(0);} // Native AA01/BB01 restart branch420EEE.
    };
    struct PlayerTransferCursor {
        uint32_t fullBytes=0;uint8_t partialBits=0;
        size_t bytesTouched()const{return size_t(fullBytes)+(partialBits?1:0);}
    };
    PlayerTransferCursor savePlayerTransfer(PlayerTransferBuffer& buffer) {
        return playerTransfer(buffer,"savePlayer",true);
    }
    PlayerTransferCursor restorePlayerTransfer(PlayerTransferBuffer& buffer) {
        // Source restore requires a fresh source-constructed Ash/inventory/weapon
        // graph and native actor bindings. Stage that host registry before this
        // call: source callbacks intentionally produce their actual side effects.
        return playerTransfer(buffer,"restorePlayer",false);
    }
private:
    PlayerTransferCursor playerTransfer(PlayerTransferBuffer& buffer,const char* method,bool saving) {
        const auto I=[](int32_t value){return jpc::Value::integer(value);};
        std::vector<jpc::Value> values(PlayerTransferBuffer::NativeBytes,I(0));
        for(size_t i=0;i<PlayerTransferBuffer::PayloadBytes;++i)
            values[i]=I(int8_t(buffer.bytes[PlayerTransferBuffer::HeaderBytes+i]));
        const auto array=vm.makeArray('B',std::move(values));
        const auto result=vm.invoke(package.methodIndex("Main",method,"([B)I"),0,{jpc::Value::reference(array)}).integer();
        const auto cursor=vm.staticField(16).integer(),bit=vm.staticField(15).integer();
        if(result!=cursor||cursor<0||bit<0||bit>7||size_t(cursor)+(bit?1:0)>PlayerTransferBuffer::PayloadBytes)
            throw jpc::Error("Original player transfer exceeds its fixed native buffer");
        // Original declared array length is0x3840, but its8-byte header is inside
        // that same native allocation. Keep that ABI and reject its unreachable
        // last8 payload bytes instead of exposing adjacent native memory.
        for(unsigned word=0;word<2;++word) {
            const uint32_t value=word?uint32_t(PlayerTransferBuffer::NativeBytes):1u;
            for(unsigned byte=0;byte<4;++byte)buffer.bytes[word*4+byte]=uint8_t(value>>(byte*8));
        }
        if(saving) {
            const auto& payload=vm.arrayValues(array);
            for(size_t i=0;i<PlayerTransferBuffer::PayloadBytes;++i)
                buffer.bytes[PlayerTransferBuffer::HeaderBytes+i]=uint8_t(payload[i].integer());
        }
        return {uint32_t(cursor),uint8_t(bit)};
    }
public:
    std::string string(jpc::Ref ref) const {
        const auto start=vm.field(ref,1).integer(),count=vm.field(ref,2).integer();
        const auto& chars=vm.arrayValues(vm.field(ref,0).reference());
        if(start<0||count<0||size_t(start)>chars.size()||size_t(count)>chars.size()-size_t(start))
            throw jpc::Error("Invalid native string range");
        std::string text;
        for(int i=0;i<count;++i) {const auto ch=chars[size_t(start+i)].integer();if(!ch)break;text.push_back(char(ch));}
        return text;
    }
    void enterScene(const std::string& name) {
        if(name.size()!=4)throw jpc::Error("Invalid script scene name");
        std::string owner="Scene";uint32_t identity=0;
        for(char letter:name) {owner.push_back(letter>='a'&&letter<='z'?char(letter-'a'+'A'):letter);identity=(identity<<8)|uint8_t(letter);}
        scene=vm.makeObject(package.classIndex(owner));
        vm.invoke(package.methodIndex(owner,"<init>","()V"),scene);
        vm.setField(level,38,jpc::Value::reference(scene));
        vm.statics[309]=jpc::Value::integer(jpc::signedBits(identity));
        vm.statics[hostField(HostFieldRole::ScenePendingPortal)]=jpc::Value::integer(0);
    }
    int interact() {
        int owner=vm.objectClass(scene);
        while(owner>=0) {
            for(size_t i=0;i<package.methods.size();++i) {
                const auto& m=package.methods[i];
                if(m.owner==owner&&m.name=="allowUse"&&m.signature=="(LAsh;I)I")
                    return vm.invoke(int(i),scene,{jpc::Value::reference(ash),jpc::Value::integer(0)}).integer();
            }
            owner=package.classes[size_t(owner)].parent;
        }
        throw jpc::Error("Scene has no original allowUse method");
    }
    int useItem(int identity) {
        return vm.invoke(package.methodIndex("Ash","useItem","(I)Z"),ash,{jpc::Value::integer(identity)}).integer();
    }
    int useSlot(int slot,int containerField=99) {
        if(containerField!=98&&containerField!=99)throw jpc::Error("Invalid inventory container");
        const auto container=vm.field(ash,uint32_t(containerField)).reference();
        if(vm.objectClass(container)!=package.classIndex("Inventory"))throw jpc::Error("Invalid player inventory");
        if(slot<0||size_t(slot)>=vm.arrayValues(vm.field(container,hostField(HostFieldRole::InventorySlots)).reference()).size())throw jpc::Error("Invalid inventory slot");
        return vm.invoke(package.methodIndex("Inventory","useItemSlot","(IZ)Z"),container,
            {jpc::Value::integer(slot),jpc::Value::integer(containerField==99?1:0)}).integer();
    }
    std::vector<ScriptInventoryItem> items(int containerField=99) const {
        if(containerField!=98&&containerField!=99&&containerField!=100)throw jpc::Error("Invalid inventory container");
        const auto container=vm.field(ash,uint32_t(containerField)).reference();
        if(vm.objectClass(container)!=package.classIndex("Inventory"))throw jpc::Error("Invalid player inventory");
        std::vector<ScriptInventoryItem> result;
        for(auto value:vm.arrayValues(vm.field(container,hostField(HostFieldRole::InventorySlots)).reference())) {
            const auto item=value.reference();
            if(vm.field(item,32).integer()==0)result.push_back({vm.field(item,33).integer(),vm.field(item,36).integer(),vm.field(item,34).integer()});
        }
        return result;
    }
    void restore(const std::vector<uint8_t>& state) {
        jpc::Interpreter candidate(package);candidate.restore(state);
        const auto nextAsh=candidate.staticField(310).reference(),nextLevel=candidate.staticField(396).reference();
        const auto nextScene=candidate.field(nextLevel,38).reference(),nextInventory=candidate.field(nextAsh,99).reference();
        if(candidate.objectClass(nextAsh)!=package.classIndex("Ash")||!nextScene||
           candidate.objectClass(nextInventory)!=package.classIndex("Inventory"))
            throw jpc::Error("Saved player or scene references are invalid");
        vm.restoreFrom(candidate);ash=nextAsh;level=nextLevel;scene=nextScene;inventory=nextInventory;
        mode=0;messageAsset=0;diagnostic.clear();guiFlags.clear();
    }
    struct Checkpoint {jpc::Interpreter state;int mode;uint32_t message;std::string diagnostic;std::map<int,int> guiFlags;};
    Checkpoint checkpoint() const {return {vm.cloneState(),mode,messageAsset,diagnostic,guiFlags};}
    void rollback(const Checkpoint& previous) {
        vm.restoreFrom(previous.state);
        ash=vm.staticField(310).reference();level=vm.staticField(396).reference();scene=vm.field(level,38).reference();inventory=vm.field(ash,99).reference();
        mode=previous.mode;messageAsset=previous.message;
        diagnostic=previous.diagnostic;guiFlags=previous.guiFlags;
    }
};
