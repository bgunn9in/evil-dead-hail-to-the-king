#include "script_game.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

using jpc::Value;
static Value I(int32_t n){return Value::integer(n);}
static Value R(jpc::Ref n){return Value::reference(n);}
static constexpr std::array<int,9> AmmoFields{{106,108,107,112,113,104,105,110,111}};
static std::string Stage;
static std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw jpc::Error("Missing player transfer oracle: "+path.string());
    return {std::istreambuf_iterator<char>(stream),{}};
}
struct PlayerCase {
    ScriptGame game;
    std::array<int,4> pose{};
    std::vector<int> actions;
    explicit PlayerCase(const std::filesystem::path& file):game(file,false) {
        Stage="construct original player graph: "+file.string();
        auto& vm=game.vm;const auto& p=game.package;
        assert(!game.ash&&!game.level&&!game.scene&&!game.inventory);
        assert(vm.staticField(310).null()&&vm.staticField(396).null());
        const auto bind=[&](const char* name,const char* signature,jpc::Interpreter::Native native) {
            vm.bind("ed/base/External",name,signature,std::move(native));
        };
        const std::array<const char*,4> getters{{"actorGetPositionX","actorGetPositionY","actorGetPositionZ","actorGetRotY"}};
        for(size_t axis=0;axis<getters.size();++axis)bind(getters[axis],"(II)I",[this,axis](auto&,auto,const auto& args) {
            actor(args);return I(pose[axis]);
        });
        bind("actorSetPosition","(IIIII)V",[this](auto&,auto,const auto& args) {
            actor(args);for(size_t i=0;i<3;++i)pose[i]=args[i+2].integer();return Value{};
        });
        bind("actorFace","(III)V",[this](auto&,auto,const auto& args) {
            actor(args);pose[3]=args[2].integer();return Value{};
        });
        bind("actorSetPartVisible","(IIIII)V",[this](auto&,auto,const auto& args) {actor(args);return Value{};});
        bind("actorDoConsoleAction","(III)I",[this](auto&,auto,const auto& args) {
            actor(args);actions.push_back(args[2].integer());return I(1); // Environmental FSM acceptance only.
        });
        game.ash=vm.makeObject(p.classIndex("Ash"));
        vm.invoke(p.methodIndex("Ash","<init>","()V"),game.ash);
        vm.setField(game.ash,5,I(0));vm.setField(game.ash,6,I(0));vm.setField(game.ash,13,I(100));
        for(const auto& [field,slots,accepted,excluded]:std::array<std::array<int,4>,4>{{
                {{99,12,80,258}},{{98,5,2,256}},{{100,14,256,66}},{{101,14,80,0}}}}) {
            const auto container=vm.makeObject(p.classIndex("Inventory"));
            vm.invoke(p.methodIndex("Inventory","<init>","(Led/base/EDNpc;III)V"),container,{R(game.ash),I(slots),I(accepted),I(excluded)});
            vm.setField(game.ash,uint32_t(field),R(container));if(field==99)game.inventory=container;
        }
        // Resolve the exact Ash.initialize allocation sequence, including duplicate
        // class names. No package-specific constructor method IDs are transplanted.
        const auto& init=p.methods[size_t(p.methodIndex("Ash","initialize","()V"))];
        const size_t base=56+init.offset;
        const auto operand=[&](size_t pc){return (int(p.data.at(base+pc))<<8)|p.data.at(base+pc+1);};
        for(size_t pc=161;pc<281;pc+=12) {
            for(const auto& [offset,opcode]:std::array<std::pair<size_t,uint8_t>,6>{{
                    {0,0x2a},{1,0xbb},{4,0x59},{5,0x2a},{6,0xb7},{9,0xb5}}})
                if(p.data.at(base+pc+offset)!=opcode)throw jpc::Error("Changed original Ash weapon construction sequence");
            const auto klass=p.constant(init.owner,operand(pc+2)).bits;
            const auto ctor=p.constant(init.owner,operand(pc+7)).bits;
            const auto field=p.constant(init.owner,operand(pc+10)).bits;
            assert(ctor<p.methods.size()&&p.methods[ctor].name=="<init>"&&p.methods[ctor].signature=="(Led/base/EDNpc;)V");
            const auto weapon=vm.makeObject(int(klass));vm.invoke(int(ctor),weapon,{R(game.ash)});vm.setField(game.ash,field,R(weapon));
        }
        vm.statics[53]=R(vm.makeArray('L',{R(game.ash)}));vm.statics[54]=R(vm.makeArray('I',{I(0)}));vm.statics[310]=R(game.ash);
    }
    static void actor(const std::vector<Value>& args) {
        if(args.at(0).integer()!=0||args.at(1).integer()!=0)throw jpc::Error("Unknown source actor in transfer fixture");
    }
    void populate(bool equipped) {
        Stage="populate source player";
        auto& vm=game.vm;pose={123,-45,678,137};
        for(const auto& [field,value]:std::array<std::pair<int,int>,6>{{{13,73},{1251,1},{1269,0},{1262,0x12345678},{1238,3},{1270,1}}})
            vm.setField(game.ash,uint32_t(field),I(value));
        const auto add=game.package.methodIndex("Inventory","addItem","(II)I");
        assert(vm.invoke(add,game.inventory,{I(29),I(1)}).integer()!=-1);
        assert(vm.invoke(add,game.inventory,{I(119),I(2)}).integer()!=-1);
        for(size_t i=0;i<AmmoFields.size();++i)vm.setField(vm.field(game.ash,uint32_t(AmmoFields[i])).reference(),815,I(int(i)*7));
        if(equipped) {
            vm.setField(game.ash,103,vm.field(game.ash,109)); // Source659 initial Axe/Chainsaw refs.
            vm.setField(game.ash,102,vm.field(game.ash,106));
        }
        vm.statics[435]=I(1);
    }
    std::vector<int32_t> values()const {
        Stage="read source inventory/weapon state";
        const auto& vm=game.vm;std::vector<int32_t> result{vm.field(game.ash,13).integer()};
        result.insert(result.end(),pose.begin(),pose.end());
        for(int field:{99,101,98,100})for(auto value:vm.arrayValues(vm.field(vm.field(game.ash,uint32_t(field)).reference(),996).reference()))
            for(unsigned key=32;key<38;++key)result.push_back(vm.field(value.reference(),key).integer());
        for(int field:AmmoFields)result.push_back(vm.field(vm.field(game.ash,uint32_t(field)).reference(),815).integer());
        for(unsigned field:{103u,102u}) {auto value=vm.field(game.ash,field);result.push_back(value.null()?-1:vm.field(value.reference(),812).integer());}
        return result;
    }
};
static void transfers(const std::filesystem::path& scripts,const std::filesystem::path& oracles) {
    for(bool reverse:{false,true})for(bool equipped:{false,true}) {
        Stage="load independent packages";
        PlayerCase source(scripts/(reverse?"levelB.jpc":"levelA.jpc")),target(scripts/(reverse?"levelA.jpc":"levelB.jpc"));
        source.populate(equipped);const auto expected=source.values();
        ScriptGame::PlayerTransferBuffer packet;
        Stage="execute original Main.savePlayer";
        const auto cursor=source.game.savePlayerTransfer(packet);
        const auto golden=read(oracles/(equipped?"equipped.player":"unequipped.player"));
        assert(cursor.fullBytes==(equipped?446u:444u)&&cursor.partialBits==3&&cursor.bytesTouched()==golden.size());
        assert(std::equal(golden.begin(),golden.end(),packet.bytes.begin()+8));
        assert((std::array<uint8_t,8>{{1,0,0,0,0x40,0x38,0,0}}==
                std::array<uint8_t,8>{{packet.bytes[0],packet.bytes[1],packet.bytes[2],packet.bytes[3],packet.bytes[4],packet.bytes[5],packet.bytes[6],packet.bytes[7]}}));
        Stage="execute original Main.restorePlayer";
        const auto restored=target.game.restorePlayerTransfer(packet);
        assert(restored.fullBytes==cursor.fullBytes&&restored.partialBits==cursor.partialBits);
        assert(target.values()==expected&&target.game.vm.staticField(435).integer()==0);
        assert(target.game.vm.field(target.game.ash,1262).integer()==-1&&target.game.vm.field(target.game.ash,1269).integer()==1);
        assert(target.game.vm.field(target.game.ash,1251).integer()==1&&target.game.vm.field(target.game.ash,1270).integer()==1);
        if(equipped)assert(target.actions==std::vector<int>({79,84}));else assert(target.actions.empty());
        source.game.vm.setField(source.game.ash,13,I(9));assert(target.game.vm.field(target.game.ash,13).integer()==73);
        // Native transfer buffer persists: partial byte lower5bits and untouched
        // trailing bytes survive the next source save, rather than being truncated.
        source.game.vm.setField(source.game.ash,13,I(73));packet.bytes.fill(0xa5);
        const auto again=source.game.savePlayerTransfer(packet);
        assert(again.fullBytes==cursor.fullBytes&&again.partialBits==3);
        assert(packet.bytes[8+golden.size()-1]==uint8_t((golden.back()&0xe0)|5));
        assert(packet.bytes.back()==0xa5);
        packet.clear();assert(std::all_of(packet.bytes.begin(),packet.bytes.end(),[](uint8_t value){return value==0;}));
    }
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw jpc::Error("Usage: player-transfer-tests <scripts-directory> <python-oracles-directory>");
        transfers(argv[1],argv[2]);
        std::cout<<"Original player-only A/B transfer: four independent-VM cases, Python byte goldens, partial/persistent fixed buffer passed\n";
    }catch(const std::exception& error){std::cerr<<Stage<<": "<<error.what()<<'\n';return 1;}
}
