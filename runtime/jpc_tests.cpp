#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

using jpc::Value;
static Value I(int32_t n) {return Value::integer(n);}
static Value R(jpc::Ref n) {return Value::reference(n);}
static void pythonTrace(const jpc::Interpreter& vm,uint64_t expected) {
    // Python case oracle: FNV-1a64 over the complete (method,PC) trace as 32-bit little-endian pairs.
    uint64_t hash=14695981039346656037ull;
    for(const auto& [method,pc]:vm.trace)for(uint32_t value:{uint32_t(method),pc})for(unsigned n=0;n<4;++n)
        hash=(hash^uint8_t(value>>(8*n)))*1099511628211ull;
    if(hash!=expected)std::cerr<<"Source trace mismatch: "<<std::hex<<hash<<" != "<<expected<<std::dec<<'\n';
    assert(hash==expected);
}

struct TestMethod {
    std::vector<uint8_t> code;
    std::string name="run",signature="()I";
    int owner=0,flags=8,locals=8,stack=16;
};
static std::vector<uint8_t> fixture(const std::vector<TestMethod>& definitions,
                                  std::map<int,uint32_t> constants={}) {
    std::vector<uint8_t> strings,code;
    auto string=[&](const std::string& s) {int start=int(strings.size());strings.insert(strings.end(),s.begin(),s.end());strings.push_back(0);return start;};
    std::vector<int> names,signatures;
    const std::vector<int> classes={string("Base"),string("Derived"),string("ed/base/External"),string("java/lang/String")};
    for(const auto& m:definitions) {names.push_back(string(m.name));signatures.push_back(string(m.signature));code.insert(code.end(),m.code.begin(),m.code.end());}
    std::vector<uint8_t> result;
    auto le16=[&](int v) {result.push_back(uint8_t(v));result.push_back(uint8_t(uint32_t(v)>>8));};
    auto le32=[&](uint32_t v) {for(int n=0;n<4;++n)result.push_back(uint8_t(v>>(8*n)));};
    const uint32_t header[]={16,uint32_t(definitions.size()),4,uint32_t(code.size()),uint32_t(strings.size()),256,256,0,0,0,0,0,0,0};
    for(auto n:header)le32(n);result.insert(result.end(),code.begin(),code.end());
    uint32_t offset=0;
    for(size_t n=0;n<definitions.size();++n) {
        const auto& m=definitions[n];const int fields[]={names[n],signatures[n],m.flags,0,0,m.owner,0,m.stack,m.locals,int(m.code.size())};
        for(int field:fields)le16(field);le32(offset);offset+=uint32_t(m.code.size());
    }
    for(int n=0;n<16*8;++n)result.push_back(0);
    for(size_t n=0;n<classes.size();++n) {const int fields[]={classes[n],0,0,0,0,0,int(definitions.size()),0,n==1||n==3?0:-1,int(n)};for(int field:fields)le16(field);}
    result.insert(result.end(),strings.begin(),strings.end());
    for(int n=0;n<256;++n)le16(n);
    for(int n=0;n<256;++n) {auto at=constants.find(n);le32(at==constants.end()?uint32_t(n):at->second);}
    le32(0);return result;
}
template<class Fn> static void fails(Fn fn,const std::string& text) {
    bool thrown=false;try {fn();}catch(const jpc::Error& e){thrown=true;if(std::string(e.what()).find(text)==std::string::npos){std::cerr<<"Expected "<<text<<", got "<<e.what()<<'\n';assert(false);}}
    assert(thrown);
}
static void repairChecksum(std::vector<uint8_t>& bytes) {
    uint32_t hash=2166136261u;for(size_t i=0;i<bytes.size()-4;++i)hash=(hash^bytes[i])*16777619u;
    for(unsigned b=0;b<4;++b)bytes[bytes.size()-4+b]=uint8_t(hash>>(b*8));
}
static void pureTests() {
    jpc::Package arithmetic(fixture({{{0x12,100,0x04,0x60,0xac}}},{{100,0x7fffffff}}));
    jpc::Interpreter a(arithmetic);assert(a.invoke(0).integer()==std::numeric_limits<int32_t>::min());
    for(const auto& pair:std::vector<std::pair<std::vector<uint8_t>,int32_t>>{
        {{0x10,0xf9,0x06,0x6c,0xac},-2},{{0x10,0xf9,0x06,0x70,0xac},-1},
        {{0x02,0x04,0x7c,0xac},0x7fffffff},{{0x04,0x10,32,0x78,0xac},1},
        {{0x11,0xff,0xff,0x92,0xac},65535},{{0x02,0x04,0x7a,0xac},-1}}) {
        jpc::Package p(fixture({{pair.first}}));jpc::Interpreter vm(p);assert(vm.invoke(0).integer()==pair.second);
    }
    jpc::Package arrays(fixture({{{0x06,0xbc,10,0x59,0x04,0x10,9,0x4f,0x04,0x2e,0xac}}}));
    jpc::Interpreter ar(arrays);assert(ar.invoke(0).integer()==9);
    jpc::Package arrayCasts(fixture({
        {{0x2a,0xc0,0,0,0xb0},"cast","(Ljava/lang/Object;)Ljava/lang/Object;"},
        {{0x2a,0xc1,0,0,0xac},"isObject","(Ljava/lang/Object;)I"},
        {{0x2a,0xc0,0,1,0xb0},"wrongCast","(Ljava/lang/Object;)Ljava/lang/Object;"},
        {{0x2a,0xc0,0,0,0x03,0x33,0xac},"byteValue","(Ljava/lang/Object;)I"}}));
    arrayCasts.classes[0].name="java/lang/Object";jpc::Interpreter casts(arrayCasts);
    for(char kind:std::string("BCSIZL")) {
        const auto array=casts.makeArray(kind,{kind=='L'?R(0):I(0)});
        assert(casts.invoke(0,0,{R(array)}).reference()==array&&casts.invoke(1,0,{R(array)}).integer()==1);
        fails([&]{casts.invoke(2,0,{R(array)});},"class cast");
    }
    assert(casts.invoke(1,0,{R(0)}).integer()==0);
    assert(casts.invoke(3,0,{R(casts.makeArray('B',{I(-5)}))}).integer()==-5);
    jpc::Package virtualCalls(fixture({
        {{0x2a,0xb4,0,5,0xac},"get","()I",0,1},
        {{0x2a,0xb4,0,5,0x05,0x68,0xac},"get","()I",1,1},
        {{0x2a,0x10,9,0xb5,0,5,0x2a,0xb6,0,0,0xac},"call","()I",0,1}}));
    jpc::Interpreter v(virtualCalls);const auto object=v.makeObject(1);
    assert(v.invoke(2,object).integer()==18);assert(v.field(object,5).integer()==9);
    fails([&]{v.invoke(2,v.makeObject(2));},"Invalid instance receiver");
    jpc::Package native(fixture({{{0xb1},"effect","()V",2,8}}));jpc::Interpreter n(native);
    fails([&]{n.invoke(0);},"Unbound native method");int calls=0;
    n.bind("ed/base/External","effect","()V",[&](auto&,auto,const auto&){++calls;return Value{};});n.invoke(0);assert(calls==1);
    for(const auto& pair:std::vector<std::pair<std::vector<uint8_t>,std::string>>{
        {{0xa7,0,0},"Instruction budget"},{{0x0b,0xac},"Unsupported opcode"},
        {{0x04,0x03,0x6c,0xac},"division by zero"},{{0x1b,0xac},"uninitialized"},
        {{0xac},"underflow"},{{0x00},"fell through"},{{0xa7,0xff,0xff},"Invalid branch"}}) {
        jpc::Package p(fixture({{pair.first}}));jpc::Interpreter vm(p,9);fails([&]{vm.invoke(0);},pair.second);
    }
    auto bytes=fixture({{{0x04,0xac}}});bytes.pop_back();bytes.resize(bytes.size()-256*4);fails([&]{jpc::Package p(bytes);},"bounds");
    for(const auto& bad:{"I)V","(V)V","(I","()","(Lbad)V","()Vjunk","([V)V"})fails([&]{jpc::descriptor(bad);},"descriptor");
    jpc::Package arrayRead(fixture({{{0x2a,0x03,0x2e,0xac},"read","([I)I"}}));jpc::Interpreter reader(arrayRead);
    fails([&]{reader.invoke(0,0,{R(reader.makeArray('I',{}))});},"bounds");
    fails([&]{reader.invoke(0,0,{R(reader.makeArray('C',{I(42)}))});},"type mismatch");
    assert(reader.invoke(0,0,{R(reader.makeArray('I',{I(42)}))}).integer()==42);
    jpc::Package yielding(fixture({
        {{0xb1},"threadSleepEvent","(III)V",2,8},
        {{0x02,0x02,0x10,26,0xb8,0,0,0x02,0x02,0x10,28,0xb8,0,0,0x10,7,0xac},"child"},
        {{0x10,5,0xb8,0,1,0x60,0xac},"parent"}}));
    jpc::Interpreter y(yielding);const auto thread=y.start(2);
    assert(y.wait(thread).event==26);const auto parked=y.snapshot();
    y.notify(99,99,26);assert(!y.resume(thread));assert(y.wait(thread).event==28);
    const auto secondWait=y.snapshot();y.restore(parked);assert(y.wait(thread).event==26);
    y.restore(secondWait);assert(y.wait(thread).event==28);
    y.notify(99,99,28);assert(y.resume(thread));assert(y.result(thread).integer()==12);
    fails([&]{y.start(-1);},"Invalid script method");
    jpc::Package resumedLoop(fixture({
        {{0xb1},"threadSleepFrame","(I)V",2,8},
        {{0x03,0xb8,0,0,0xa7,0,0},"loop"}}));
    jpc::Interpreter loop(resumedLoop,9);const auto sleeper=loop.start(1);loop.advanceFrame();
    fails([&]{loop.resume(sleeper);},"Instruction budget");
    const std::vector<uint8_t> child={0x02,0x02,0x10,26,0xb8,0,0,0x10,7,0xac};
    jpc::Package exactCall(fixture({
        {{0xb1},"threadSleepEvent","(III)V",2,8},
        {child,"child","()I",0,8},
        {{0x10,5,0xb8,0,1,0x60,0xac},"parent"},
        {child,"child","()I",1,8}}));
    jpc::Interpreter exact(exactCall);exact.start(2);auto exactSave=exact.snapshot(),badExact=exactSave;
    badExact[104]=3;repairChecksum(badExact);fails([&]{exact.restore(badExact);},"caller chain");assert(exact.snapshot()==exactSave);
    jpc::Package virtualCall(fixture({
        {{0xb1},"threadSleepEvent","(III)V",2,8},
        {child,"child","()I",0,1},
        {{0x2a,0xb6,0,1,0xac},"parent","(LBase;)I"},
        {child,"child","()I",1,1}}));
    jpc::Interpreter dispatch(virtualCall);const auto derived=dispatch.makeObject(1);dispatch.start(2,0,{R(derived)});
    const auto virtualSave=dispatch.snapshot();auto badVirtual=virtualSave;
    badVirtual[120]=1;repairChecksum(badVirtual);fails([&]{dispatch.restore(badVirtual);},"caller chain");assert(dispatch.snapshot()==virtualSave);
    dispatch.restore(virtualSave);dispatch.notify(0,0,26);dispatch.runReady();
}
static void schedulerTests() {
    jpc::Package p(fixture({
        {{0xb1},"threadSleepEvent","(III)V",2,8},
        {{0xb1},"threadSleepFrame","(I)V",2,8},
        {{0xb1},"threadSuspendOther","(I)V",2,8},
        {{0xb1},"threadAwakeOther","(I)V",2,8},
        {{0x04,0x03,0x10,26,0xb8,0,0,0x10,9,0xac},"wait"},
        {{0x05,0xb8,0,1,0x10,7,0xac},"timer"},
        {{0x10,42,0xb3,0,3,0x10,11,0xac},"queued"},
        {{0x04,0x03,0x10,26,0xb8,0,0,0x2a,0x03,0x2e,0xac},"arrayWait","([I)I"}}));
    using VM=jpc::Interpreter;VM vm(p);
    const auto queued=vm.enqueue(6);assert(vm.staticField(3).integer()==0&&vm.status(queued)==VM::ThreadStatus::Ready);
    const auto queuedSave=vm.snapshot();vm.runReady();assert(vm.result(queued).integer()==11&&vm.staticField(3).integer()==42);
    vm.restore(queuedSave);assert(vm.staticField(3).integer()==0&&vm.runReady()==std::vector<VM::ThreadID>{queued});
    assert(vm.result(queued).integer()==11);vm.forget(queued);
    const auto held=vm.start(4);vm.invoke(2,0,{I(int32_t(held))});assert(vm.status(held)==VM::ThreadStatus::Held);
    const auto heldSave=vm.snapshot();assert(vm.notify(1,0,25).empty()&&vm.runReady().empty());
    vm.invoke(3,0,{I(int32_t(held))});assert(vm.status(held)==VM::ThreadStatus::Waiting);
    vm.restore(heldSave);assert(vm.status(held)==VM::ThreadStatus::Held);
    assert(vm.notify(1,0,26).empty()&&vm.runReady().empty()); // Satisfied while held, still cannot execute.
    const auto satisfiedSave=vm.snapshot();vm.restore(satisfiedSave);vm.invoke(3,0,{I(int32_t(held))});
    assert(vm.status(held)==VM::ThreadStatus::Ready&&vm.resume(held)&&vm.result(held).integer()==9);vm.forget(held);
    const auto timer=vm.start(5);vm.hold(timer);vm.advanceFrame();vm.advanceFrame();vm.advanceFrame();
    const auto expiredSave=vm.snapshot();vm.restore(expiredSave);assert(vm.runReady().empty());
    vm.awake(timer);assert(vm.status(timer)==VM::ThreadStatus::Ready&&vm.resume(timer)&&vm.result(timer).integer()==7);
    vm.forget(timer);const auto waiting=vm.start(4);const auto next=vm.enqueue(6);assert(next>waiting);
    auto saved=vm.snapshot();vm.notify(1,0,26);vm.restore(saved);assert(vm.wait(waiting).event==26);
    // Checksummed malformed snapshots must leave the current VM unchanged.
    auto checksum=[](std::vector<uint8_t>& bytes) {uint32_t hash=2166136261u;
        for(size_t i=0;i<bytes.size()-4;++i)hash=(hash^bytes[i])*16777619u;
        for(unsigned b=0;b<4;++b)bytes[bytes.size()-4+b]=uint8_t(hash>>(b*8));};
    auto corrupt=saved;corrupt[6]='9';fails([&]{vm.restore(corrupt);},"header");assert(vm.snapshot()==saved);
    corrupt=saved;corrupt[corrupt.size()-5]^=1;fails([&]{vm.restore(corrupt);},"checksum");assert(vm.snapshot()==saved);
    // This empty-heap snapshot has scheduler thread count at40.
    corrupt=queuedSave;for(unsigned b=0;b<4;++b)corrupt[40+b]=0xff;checksum(corrupt);
    fails([&]{vm.restore(corrupt);},"thread budget");assert(vm.snapshot()==saved);
    VM malformed(p);malformed.start(4);const auto continuation=malformed.snapshot();
    // One parked frame after the empty heap/thread header: cursor112, pending descriptor120.
    corrupt=continuation;for(unsigned b=0;b<4;++b)corrupt[112+b]=0;checksum(corrupt);
    fails([&]{malformed.restore(corrupt);},"frame cursor");assert(malformed.snapshot()==continuation);
    corrupt=continuation;corrupt[120]='I';checksum(corrupt);
    fails([&]{malformed.restore(corrupt);},"pending result");assert(malformed.snapshot()==continuation);
    corrupt=continuation;corrupt[corrupt.size()-8]=1;checksum(corrupt);
    fails([&]{malformed.restore(corrupt);},"callback ownership");assert(malformed.snapshot()==continuation);
    VM references(p);const auto array=references.makeArray('I',{I(123)});
    const auto arrayWait=references.start(7,0,{R(array)});const auto arraySave=references.snapshot();
    references.arrayValues(array)[0]=I(0);references.restore(arraySave);references.notify(1,0,26);
    assert(references.resume(arrayWait)&&references.result(arrayWait).integer()==123);
    VM deadline(p);const auto delayed=deadline.start(5);deadline.advanceFrame();const auto deadlineSave=deadline.snapshot();
    deadline.advanceFrame();deadline.advanceFrame();assert(deadline.status(delayed)==VM::ThreadStatus::Ready);
    deadline.restore(deadlineSave);assert(deadline.advanceFrame().empty()&&deadline.advanceFrame()==std::vector<VM::ThreadID>{delayed});
    auto legacy=queuedSave;legacy[6]='1';legacy.erase(legacy.begin()+28,legacy.end()-4);checksum(legacy);
    vm.restore(legacy);assert(!vm.pendingThreads()&&vm.staticField(3).integer()==0);
    VM teardown(p);const auto oldWait=teardown.start(4),oldTimer=teardown.start(5),oldQueued=teardown.enqueue(6);
    const auto oldDeadline=teardown.wait(oldTimer).frame;teardown.hold(oldTimer);teardown.advanceFrame();teardown.statics[3]=I(77);const auto beforeClear=teardown.snapshot();
    teardown.clearSceneThreads();assert(!teardown.pendingThreads()&&teardown.staticField(3).integer()==77&&teardown.runReady().empty());
    for(auto id:{oldWait,oldTimer,oldQueued})fails([&]{teardown.status(id);},"Invalid script thread");
    const auto later=teardown.start(5);assert(later>oldQueued&&teardown.wait(later).frame==oldDeadline+1);
    teardown.restore(beforeClear);assert(teardown.pendingThreads()==3&&teardown.status(oldTimer)==VM::ThreadStatus::Held);
    teardown.clearSceneThreads();assert(teardown.notify(1,0,26).empty());
    std::cout<<"queued per-actor scheduling/held events/deadlines/V3 continuations/V1 restore: passed\n";
}
static void phaseMaskTests() {
    jpc::Package p(fixture({
        {{0xb1},"threadSleepFrame","(I)V",2,8},
        {{0xb1},"callback","()V",2,8},
        {{0xb2,0,3,0x04,0x60,0xb3,0,3,0x03,0xb8,0,0,0xb2,0,3,0x04,0x60,0xb3,0,3,0xb1},"ui","()V"},
        {{0x08,0xb3,0,4,0xb8,0,1,0xb2,0,4,0x04,0x60,0xb3,0,4,0xb2,0,4,0xac},"outer"},
        {{0x10,42,0xb3,0,3,0xb1},"instant","()V"},
        {{0x1a,0x99,0,10,0x1a,0x04,0x64,0xb8,0,5,0xb1,0xb8,0,1,0xb1},"recursive","(I)V"}}));
    using VM=jpc::Interpreter;VM vm(p);VM::ThreadID callback=0;
    vm.bind("ed/base/External","callback","()V",[&](auto& nested,auto,const auto&) {
        callback=nested.startCallback(2,0,{},0);return Value{};
    });
    // A native may synchronously start a separate UI context while a parent frame is live.
    assert(vm.invoke(3).integer()==6&&vm.staticField(3).integer()==1&&vm.staticField(4).integer()==6);
    assert(callback&&vm.status(callback)==VM::ThreadStatus::Waiting);
    assert(std::find(vm.trace.begin(),vm.trace.end(),std::make_pair(2,0u))!=vm.trace.end());
    assert((vm.trace.back()==std::make_pair(3,18u)));
    const auto uiSave=vm.snapshot();
    assert(vm.advanceFrame(1)==std::vector<VM::ThreadID>{callback});
    assert(vm.runReady(1)==std::vector<VM::ThreadID>{callback}&&vm.staticField(3).integer()==2&&!vm.pendingThreads());
    fails([&]{vm.status(callback);},"Invalid script thread"); // Detached UI contexts free their completed slot.
    vm.restore(uiSave);assert(vm.staticField(3).integer()==1);
    auto corrupt=uiSave;corrupt[corrupt.size()-12]=2;repairChecksum(corrupt);
    fails([&]{vm.restore(corrupt);},"thread flags");assert(vm.snapshot()==uiSave);
    assert(vm.advanceFrame(1)==std::vector<VM::ThreadID>{callback});vm.runReady(1);assert(vm.staticField(3).integer()==2);
    assert(!vm.startCallback(4)&&vm.staticField(3).integer()==42&&!vm.pendingThreads());
    const auto game=vm.start(0,0,{I(0)},1),ui=vm.startCallback(2,0,{},0);
    for(unsigned n=0;n<3;++n){vm.advanceFrame(1);vm.runReady(1);}
    assert(vm.status(game)==VM::ThreadStatus::Waiting&&vm.staticField(3).integer()==44);
    fails([&]{vm.status(ui);},"Invalid script thread");
    const auto masked=vm.snapshot();vm.restore(masked);
    assert(vm.advanceFrame()==std::vector<VM::ThreadID>{game}&&vm.runReady()==std::vector<VM::ThreadID>{game});vm.forget(game);
    const auto ready=vm.enqueue(4);assert(vm.runReady(1).empty());vm.runReady();vm.forget(ready);
    // Legacy V2 records had no flags/ownership words and used gameplay flag1.
    VM old(p);const auto oldGame=old.start(0,0,{I(0)});auto v2=old.snapshot();v2[6]='2';v2.erase(v2.end()-12,v2.end()-4);repairChecksum(v2);
    old.restore(v2);old.advanceFrame(1);assert(old.runReady(1).empty()&&old.status(oldGame)==VM::ThreadStatus::Waiting);
    old.advanceFrame();old.runReady();assert(old.status(oldGame)==VM::ThreadStatus::Complete);
    fails([&]{old.enqueue(4,0,{},2);},"thread flags");fails([&]{old.startCallback(3);},"return void");
    // A failed nested callback restores the parent's interpreter control state.
    vm.bind("ed/base/External","callback","()V",[&](auto& nested,auto,const auto&){nested.startCallback(1);return Value{};});
    fails([&]{vm.invoke(3);},"callback depth");assert(!vm.pendingThreads());
    fails([&]{vm.invoke(5,0,{I(48)});},"script call depth");assert(!vm.pendingThreads());
    assert(!vm.startCallback(4)&&vm.staticField(3).integer()==42);
    std::cout<<"native scheduler mask/UI0 nested callbacks/gameplay1 deadlines/V2 restore/V3 atomic flags: passed\n";
}
struct ActorCase {
    jpc::Interpreter vm;
    jpc::Ref ash=0,face=0;
    bool transition=true,home=true;
    std::vector<std::pair<std::string,std::vector<int32_t>>> calls;
    std::map<int,std::vector<int32_t>> colors;
    std::map<int,int32_t> maya;
    explicit ActorCase(const jpc::Package& p,int health=70):vm(p) {
        ash=vm.makeObject(p.classIndex("Ash"),{{5,I(0)},{6,I(1)},{7,I(-1)},{8,I(-1)},{13,I(health)},{1259,I(-1)},{1260,I(-1)}});
        const auto items=vm.makeArray('I',{I(11)});
        face=vm.makeObject(p.classIndex("GameAshFace"),{{379,R(ash)},{835,R(items)},{836,I(7)}});
        vm.statics={{53,R(vm.makeArray('L',{R(ash)}))},{54,R(vm.makeArray('I',{I(0),I(0)}))},
                    {60,I(0)},{309,I(0x616b3031)},{394,I(0)},{398,I(1234)},{849,R(face)}};
        for(const auto& method:p.methods)if(p.classes[size_t(method.owner)].name=="ed/base/External"&&
            std::set<std::string>{"guiItemSetMayaVal","guiItemSetColor","guiSysAtOffset","guiSysAddFlag","guiSysRemoveFlag",
                "random","stateTransitionIsPossible","actorDoConsoleAction","vfxSpawnActorJoint","sndMgrGetSNDID","sndMgrPlaySoundVol"}.count(method.name)) {
            const auto name=method.name,signature=method.signature;
            vm.bind("ed/base/External",name,signature,[this,name,signature](auto&,auto,const std::vector<Value>& values) {
                std::vector<int32_t> args;for(auto v:values)args.push_back(v.integer());calls.emplace_back(name,args);
                if(name=="guiItemSetMayaVal")maya[args[0]]=args[2];
                if(name=="guiItemSetColor")colors[args[0]]=std::vector<int32_t>(args.begin()+1,args.end());
                if(name=="guiSysAtOffset")return I(home?1:0);
                if(name=="stateTransitionIsPossible")return I(transition?1:0);
                if(name=="sndMgrGetSNDID")return I(17);
                if(name=="sndMgrPlaySoundVol")return I(23);
                return signature.back()=='V'?Value{}:I(0);
            });
        }
        vm.initialize("EventManager");
    }
    int hit(int damage,int type=36) {
        const auto m=vm.package.methodIndex("ActorDB","processMessage","(IIIIIIIIIII)I");
        assert(vm.invoke(m,0,{I(1),I(0),I(0),I(0),I(2),I(0),I(type),I(damage),I(0),I(0),I(0)}).integer()==1);
        return vm.field(ash,13).integer();
    }
};
static void actorTests(const jpc::Package& p) {
    ActorCase c(p);assert(c.hit(20)==50);assert(c.maya[11]==50);assert(c.colors[11]==std::vector<int32_t>({255,255,0,255}));
    pythonTrace(c.vm,0x4bc1ea5687e9ac33ull);
    assert(c.vm.field(c.face,380).integer()==60);assert(c.vm.field(c.ash,8).integer()==35);
    assert(c.vm.field(c.ash,130).integer()==1234);assert(c.vm.field(c.ash,11).integer()==0);
    assert(std::find(c.vm.trace.begin(),c.vm.trace.end(),std::make_pair(649,514u))!=c.vm.trace.end());
    assert(c.vm.staticField(179).integer()==1);assert(c.vm.arrayValues(c.vm.staticField(181).reference())[0].integer()==36);
    assert(c.hit(100)==0);assert(c.colors[11]==std::vector<int32_t>({255,0,0,255}));
    assert(c.vm.invoke(p.methodIndex("GenericNpc","doReactionDeath","()I"),c.ash).integer()==32);
    for(const auto& [type,action]:std::vector<std::pair<int,int>>{{36,35},{37,34},{38,32},{39,33},{40,36},{41,37},{42,38}}) {
        ActorCase a(p,100);assert(a.hit(1,type)==99);assert(a.vm.field(a.ash,8).integer()==action);
    }
    ActorCase guarded(p);guarded.vm.statics[394]=I(1);assert(guarded.hit(20)==70);assert(guarded.calls.empty());
    ActorCase suppressed(p);suppressed.vm.statics[60]=I(1);assert(suppressed.hit(20)==70);assert(!suppressed.calls.empty());
    ActorCase fallback(p);fallback.transition=false;fallback.hit(1,40);
    assert(std::find(fallback.calls.begin(),fallback.calls.end(),std::make_pair(std::string("sndMgrGetSNDID"),std::vector<int32_t>{65549}))!=fallback.calls.end());
    ActorCase slide(p);slide.home=false;slide.hit(1);assert(slide.vm.field(slide.face,841).integer()&2);
    std::cout<<"original actor dispatcher/health/portrait: passed\n";
}
static void coroutineTests(const jpc::Package& p) {
    ActorCase actor(p);unsigned actions=0;
    actor.vm.bind("ed/base/External","actorDoConsoleAction","(III)I",[&](auto&,auto,const auto& args) {
        assert(args[2].integer()==35);++actions;return I(1);
    });
    const auto method=p.methodIndex("ActorDB","processMessage","(IIIIIIIIIII)I");
    const auto thread=actor.vm.start(method,0,{I(1),I(0),I(0),I(0),I(2),I(0),I(36),I(20),I(0),I(0),I(0)});
    assert(actor.vm.status(thread)==jpc::Interpreter::ThreadStatus::Waiting);
    const auto wait=actor.vm.wait(thread);assert(wait.actorType==1&&wait.actorIndex==0&&wait.event==26);
    assert(actor.vm.field(actor.ash,13).integer()==50&&actor.vm.field(actor.ash,114).integer()==1);
    assert(actor.vm.field(actor.ash,7).integer()==-1&&actor.vm.field(actor.ash,8).integer()==35);
    assert(actor.vm.field(actor.ash,11).integer()==1&&actions==1);
    assert(actor.vm.notify(0,0,26).empty()&&actor.vm.notify(1,1,26).empty()&&actor.vm.notify(1,0,34).empty());
    const auto pendingDisk=actor.vm.snapshot();
    auto pending=actor.vm.cloneState();
    assert(actor.vm.notify(1,0,26)==std::vector<jpc::Interpreter::ThreadID>({thread}));
    assert(actor.vm.resume(thread));assert(actor.vm.result(thread).integer()==1);
    assert(actor.vm.field(actor.ash,13).integer()==50&&actor.vm.field(actor.ash,114).integer()==0);
    assert(actor.vm.field(actor.ash,7).integer()==35&&actor.vm.field(actor.ash,8).integer()==-1);
    assert(actor.vm.field(actor.ash,11).integer()==0&&actions==1);
    const auto complete=actor.vm.snapshot();
    actor.vm.restoreFrom(pending);
    assert(actor.vm.status(thread)==jpc::Interpreter::ThreadStatus::Waiting&&actor.vm.field(actor.ash,114).integer()==1);
    actor.vm.restore(complete);assert(actor.vm.field(actor.ash,114).integer()==0);
    actor.vm.restore(pendingDisk);assert(actor.vm.status(thread)==jpc::Interpreter::ThreadStatus::Waiting);
    actor.vm.notify(1,0,26);assert(actor.vm.runReady()==std::vector<jpc::Interpreter::ThreadID>({thread}));
    assert(actor.vm.field(actor.ash,13).integer()==50&&actor.vm.field(actor.ash,11).integer()==0&&actions==1);
    actor.vm.forget(thread);actor.vm.restore(complete);
    const int sleep=p.methodIndex("ed/base/External","threadSleepEvent","(III)V");
    const auto first=actor.vm.start(sleep,0,{I(-1),I(-1),I(24)});
    const auto second=actor.vm.start(sleep,0,{I(4),I(-1),I(24)});
    assert(actor.vm.notify(4,9,24)==std::vector<jpc::Interpreter::ThreadID>({first,second}));
    assert(actor.vm.runReady()==std::vector<jpc::Interpreter::ThreadID>({first,second}));
    const int frame=p.methodIndex("ed/base/External","threadSleepFrame","(I)V");
    const auto timer=actor.vm.start(frame,0,{I(1)});
    assert(actor.vm.advanceFrame().empty());assert(actor.vm.advanceFrame()==std::vector<jpc::Interpreter::ThreadID>({timer}));assert(actor.vm.resume(timer));
    fails([&]{actor.vm.invoke(sleep,0,{I(1),I(0),I(26)});},"requires start");
    std::cout<<"original accepted-hit coroutine/resume/rollback/wildcards/frame waits: passed\n";
}
static void conditionalTests(const jpc::Package& p) {
    for(int state:{0,1}) {
        jpc::Interpreter vm(p);std::map<int,int> portals;
        const std::set<std::string> names={"sndMgrGetSNDID","sndMgrPlaySoundVolLoop","lightSetColor","lightSetEnable","objSetup",
            "objSetBRadius","objFlagsSub","objSetPositionMarker","objSetOrientation","scenePortalEnable"};
        for(const auto& method:p.methods)if(p.classes[size_t(method.owner)].name=="ed/base/External"&&names.count(method.name)) {
            const auto name=method.name,signature=method.signature;
            vm.bind("ed/base/External",name,signature,[&,name,signature](auto&,auto,const std::vector<Value>& values) {
                if(name=="scenePortalEnable")portals[values[0].integer()]=values[1].integer();return signature.back()=='V'?Value{}:I(0);
            });
        }
        for(const auto& owner:{"Main","MissionCommander","Ash","GenericScene","ObjDB"})vm.initialize(owner);
        assert(vm.staticField(405).integer()==0);vm.statics[405]=I(state);
        vm.statics[310]=R(vm.makeObject(p.classIndex("Ash")));
        auto& pool=vm.arrayValues(vm.staticField(700).reference());assert(pool.size()==32);
        for(auto& item:pool)item=R(vm.makeObject(p.classIndex("Obj")));
        const auto scene=vm.makeObject(p.classIndex("SceneAK09"));
        vm.invoke(p.methodIndex("SceneAK09","activateScene","(Z)V"),scene,{I(0)});
        assert(portals[11]==(state?0:1));assert(portals[12]==(state?1:0));
        pythonTrace(vm,state?0x5b797a2907672b39ull:0x26197a770af40122ull);
        assert(vm.steps==1482);std::cout<<"original conditional door state "<<state<<": "<<vm.steps<<" instructions\n";
    }
}
struct PickupCase {
    jpc::Interpreter vm;
    jpc::Ref ash=0,scene=0,inventory=0,itemGui=0;
    std::vector<int> gameStates;
    std::vector<std::string> diagnostics;
    std::map<int,int32_t> guiData;
    explicit PickupCase(const jpc::Package& p):vm(p) {
        const std::set<std::string> names={"actorInTrigger","actorIsFacingMarker","gameStateChange","debugPrintString",
            "debugPrintInt","guiSysAddFlag","guiItemSetDataID","guiSysAtOffset"};
        for(const auto& method:p.methods)if(p.classes[size_t(method.owner)].name=="ed/base/External"&&names.count(method.name)) {
            const auto name=method.name,signature=method.signature;
            vm.bind("ed/base/External",name,signature,[this,name,signature](auto& host,auto,const std::vector<Value>& args) {
                if(name=="actorInTrigger") {
                    // Python's independent original AK01 geometry query at (2.45914788,8.35600605) returns these predicates.
                    assert(args[0].integer()==0&&args[1].integer()==0&&args[2].integer()==76);return I(1);
                }
                if(name=="actorIsFacingMarker") {
                    assert(args[0].integer()==0&&args[1].integer()==0&&args[2].integer()==33&&args[3].integer()==40);return I(1);
                }
                if(name=="gameStateChange")gameStates.push_back(args[0].integer());
                if(name=="guiItemSetDataID")guiData[args[0].integer()]=args[1].integer();
                if(name=="guiSysAtOffset") {assert(args[0].integer()==200);return I(args[1].integer()==0&&args[2].integer()==0?1:0);}
                if(name=="debugPrintString") {
                    const auto string=args[0].reference();const auto& chars=host.arrayValues(host.field(string,0).reference());
                    const int32_t start=host.field(string,1).integer(),count=host.field(string,2).integer();
                    assert(start>=0&&count>=0&&size_t(start)+size_t(count)<=chars.size());std::string value;
                    for(int32_t n=0;n<count&&chars[size_t(start+n)].integer();++n)value.push_back(char(chars[size_t(start+n)].integer()));
                    diagnostics.push_back(value);
                }
                return signature.back()=='V'?Value{}:I(0);
            });
        }
        ash=vm.makeObject(p.classIndex("Ash"),{{5,I(0)},{6,I(0)}});scene=vm.makeObject(p.classIndex("SceneAK01"));
        const auto level=vm.makeObject(p.classIndex("ed/base/EDLevel"),{{38,R(scene)}});
        itemGui=vm.makeObject(p.classIndex("InvItem"));
        const auto text=vm.makeObject(p.classIndex("GameTextBox"),{{835,R(vm.makeArray('I',{I(100)}))},{836,I(200)}});
        vm.statics={{310,R(ash)},{396,R(level)},{418,I(0)},{868,R(itemGui)},{851,R(text)}};vm.initialize("SpawnCommander");
        inventory=vm.makeObject(p.classIndex("Inventory"));
        vm.invoke(p.methodIndex("Inventory","<init>","(Led/base/EDNpc;III)V"),inventory,{R(ash),I(12),I(80),I(258)});
        vm.setField(inventory,1000,R(itemGui));vm.setField(ash,99,R(inventory));
    }
    int interact() {return vm.invoke(vm.package.methodIndex("SceneAK01","allowUse","(LAsh;I)I"),scene,{R(ash),I(0)}).integer();}
    std::vector<std::vector<int32_t>> items() {
        std::vector<std::vector<int32_t>> result;
        for(auto ref:vm.arrayValues(vm.field(inventory,996).reference()))if(vm.field(ref.reference(),32).integer()==0)
            result.push_back({vm.field(ref.reference(),34).integer(),vm.field(ref.reference(),33).integer(),
                              vm.field(ref.reference(),36).integer(),vm.field(ref.reference(),35).integer()});
        return result;
    }
};
static void pickupTests(const jpc::Package& p) {
    PickupCase picked(p);assert(picked.interact()==116);assert(picked.vm.steps==541);
    pythonTrace(picked.vm,0xe94d8254650a9cbCull);
    assert(picked.items()==std::vector<std::vector<int32_t>>({{0,119,1,60}}));
    assert(picked.vm.staticField(418).integer()==1);assert(picked.vm.field(picked.itemGui,949).integer()==1);
    assert(picked.gameStates==std::vector<int>({2}));
    assert(picked.interact()==0);assert(picked.items()==std::vector<std::vector<int32_t>>({{0,119,1,60}}));
    pythonTrace(picked.vm,0x649c521f3c329f51ull);
    assert(picked.guiData[100]==0x318a0804);
    PickupCase full(p);
    for(int item:{3,4,5,6,7,8,9,11,15,24,29,30})assert(full.vm.invoke(p.methodIndex("Inventory","addItem","(II)I"),full.inventory,{I(item),I(1)}).integer()!=-1);
    const auto before=full.items();assert(full.interact()==0);assert(full.items()==before);assert(full.vm.staticField(418).integer()==0);
    pythonTrace(full.vm,0x1fe812ccaab2c772ull);
    assert(std::find(full.diagnostics.begin(),full.diagnostics.end(),"Ash wasn't able to pickup the item")!=full.diagnostics.end());
    std::cout<<"original pickup/inventory/repeat/capacity/string literals: passed\n";
}
int main(int argc,char** argv) {
    try {pureTests();schedulerTests();phaseMaskTests();if(argc>1) {const auto package=jpc::Package::load(argv[1]);actorTests(package);coroutineTests(package);conditionalTests(package);pickupTests(package);} }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    std::cout<<"JPC tests passed\n";
}
