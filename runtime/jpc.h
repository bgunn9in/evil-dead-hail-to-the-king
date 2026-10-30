#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace jpc {
using Ref=uint32_t;
struct Error:std::runtime_error { using std::runtime_error::runtime_error; };
inline int32_t signedBits(uint32_t bits) { int32_t value; std::memcpy(&value,&bits,4); return value; }
struct Value {
    enum class Kind { Integer,Reference,Void,Unset } kind=Kind::Void;
    uint32_t bits=0;
    static Value integer(int32_t value) { return {Kind::Integer,uint32_t(value)}; }
    static Value reference(Ref value) { return {Kind::Reference,value}; }
    static Value unset() { return {Kind::Unset,0}; }
    int32_t integer() const { if(kind!=Kind::Integer)throw Error("Expected integer");return signedBits(bits); }
    Ref reference() const { if(kind!=Kind::Reference)throw Error("Expected reference");return bits; }
    bool null() const { return (kind==Kind::Integer||kind==Kind::Reference)&&!bits; }
    bool operator==(Value other) const { return (kind==other.kind&&bits==other.bits)||(null()&&other.null()); }
    bool operator!=(Value other) const { return !(*this==other); }
};
struct Descriptor { std::vector<std::string> args;std::string result; };
inline Descriptor descriptor(const std::string& text) {
    size_t cursor=1;
    auto take=[&]() {
        const size_t start=cursor;
        while(cursor<text.size()&&text[cursor]=='[')++cursor;
        if(cursor>=text.size())throw Error("Truncated descriptor");
        char type=text[cursor++];
        if(type=='L') {
            const size_t end=text.find(';',cursor);
            if(end==std::string::npos||end==cursor)throw Error("Invalid reference descriptor");
            cursor=end+1;
        } else if(std::string("ZBCSIJFDV").find(type)==std::string::npos)throw Error("Invalid descriptor type");
        if(type=='V'&&cursor-start!=1)throw Error("Void array descriptor");
        return text.substr(start,cursor-start);
    };
    if(text.empty()||text[0]!='(')throw Error("Invalid method descriptor");
    Descriptor value;
    while(cursor<text.size()&&text[cursor]!=')') {
        auto type=take();if(type=="V")throw Error("Void argument in descriptor");value.args.push_back(type);
    }
    if(cursor>=text.size())throw Error("Unterminated method descriptor");
    ++cursor;value.result=take();
    if(cursor!=text.size())throw Error("Trailing method descriptor data");
    return value;
}
struct Class { std::string name;int pool=0,firstMethod=0,methodCount=0,parent=-1; };
struct Method {
    std::string name,signature;int owner=0,flags=0,maxStack=0,maxLocals=0;
    uint32_t offset=0,length=0;bool hasCode=false;
};
struct Constant { int index=0;uint32_t bits=0; };
class Package {
    uint32_t u32(size_t offset) const {
        range(offset,4);return uint32_t(data[offset])|(uint32_t(data[offset+1])<<8)|
            (uint32_t(data[offset+2])<<16)|(uint32_t(data[offset+3])<<24);
    }
    uint16_t u16(size_t offset) const { range(offset,2);return uint16_t(data[offset])|(uint16_t(data[offset+1])<<8); }
    int s16(size_t offset) const { const auto value=u16(offset);return value<32768?int(value):int(value)-65536; }
    void range(size_t offset,size_t count) const {
        if(offset>data.size()||count>data.size()-offset)throw Error("JPC section exceeds package bounds");
    }
    std::string text(int offset) const {
        if(offset<0||size_t(offset)>=stringSize)throw Error("Invalid JPC string table reference");
        size_t start=stringOffset+size_t(offset),end=start;
        while(end<stringOffset+stringSize&&data[end])++end;
        if(end==stringOffset+stringSize)throw Error("Unterminated JPC string table entry");
        return std::string(data.begin()+start,data.begin()+end);
    }
    size_t stringOffset=0,stringSize=0,indexOffset=0,indexCount=0,valueOffset=0,valueCount=0;
public:
    std::vector<uint8_t> data;
    std::vector<Class> classes;
    std::vector<Method> methods;
    uint32_t fieldCount=0,codeSize=0;
    explicit Package(std::vector<uint8_t> bytes):data(std::move(bytes)) {
        if(data.size()>64ull*1024*1024)throw Error("Excessive JPC package size");
        range(0,56);
        fieldCount=u32(0);const uint32_t methodCount=u32(4),classCount=u32(8);
        codeSize=u32(12);stringSize=u32(16);valueCount=u32(20);indexCount=u32(24);
        if(fieldCount>1000000||methodCount>65536||classCount>65536||indexCount>1000000||valueCount>1000000)
            throw Error("Excessive JPC table count");
        const size_t methodOffset=56ull+codeSize,fieldOffset=methodOffset+24ull*methodCount;
        const size_t classOffset=fieldOffset+8ull*fieldCount;
        stringOffset=classOffset+20ull*classCount;indexOffset=stringOffset+stringSize;
        valueOffset=indexOffset+2ull*indexCount;range(valueOffset,4ull*valueCount);
        for(uint32_t i=0;i<classCount;++i) {
            size_t p=classOffset+20ull*i;
            Class c{text(s16(p)),s16(p+2),s16(p+8),s16(p+12),s16(p+16)};
            if(c.pool<0||size_t(c.pool)>indexCount||c.firstMethod<0||c.methodCount<0||
               size_t(c.firstMethod)>methodCount||size_t(c.methodCount)>methodCount-size_t(c.firstMethod)||
               c.parent< -1||c.parent>=int(classCount))throw Error("Invalid JPC class table reference");
            classes.push_back(std::move(c));
        }
        for(uint32_t i=0;i<methodCount;++i) {
            size_t p=methodOffset+24ull*i;
            Method m{text(s16(p)),text(s16(p+2)),s16(p+10),s16(p+4),s16(p+14),s16(p+16),
                     u32(p+20),u16(p+18),u32(p+20)!=0xffffffffu};
            if(m.owner<0||m.owner>=int(classCount)||(!m.hasCode&&!(m.flags&0x500))||
               (m.hasCode&&(m.offset>codeSize||m.length>codeSize-m.offset)))throw Error("Invalid JPC method code range");
            if(!m.hasCode)m.length=0;
            methods.push_back(std::move(m));
        }
        std::vector<uint8_t> visited(classes.size(),0);
        for(size_t i=0;i<classes.size();++i) {
            std::vector<int> chain;int at=int(i);
            while(at>=0&&!visited[size_t(at)]) {visited[size_t(at)]=1;chain.push_back(at);at=classes[size_t(at)].parent;}
            if(at>=0&&visited[size_t(at)]==1)throw Error("Cyclic JPC inheritance");
            for(int owner:chain)visited[size_t(owner)]=2;
        }
    }
    static Package load(const std::filesystem::path& path) {
        std::ifstream stream(path,std::ios::binary|std::ios::ate);
        if(!stream)throw Error("Cannot open JPC package: "+path.string());
        const auto size=stream.tellg();
        if(size<0||uint64_t(size)>64ull*1024*1024)throw Error("Invalid or excessive JPC package size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));stream.seekg(0);
        if(!bytes.empty()&&!stream.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))
            throw Error("Cannot read complete JPC package");
        return Package(std::move(bytes));
    }
    int classIndex(const std::string& name) const {
        int result=-1;
        for(size_t i=0;i<classes.size();++i)if(classes[i].name==name) {
            if(result>=0)throw Error("Ambiguous script class: "+name);result=int(i);
        }
        if(result<0)throw Error("Missing script class: "+name);return result;
    }
    int methodIndex(const std::string& owner,const std::string& name,const std::string& signature) const {
        int result=-1;
        for(size_t i=0;i<methods.size();++i) {
            const auto& m=methods[i];
            if(classes[size_t(m.owner)].name==owner&&m.name==name&&m.signature==signature) {
                if(result>=0)throw Error("Ambiguous script method: "+owner+"."+name);result=int(i);
            }
        }
        if(result<0)throw Error("Missing script method: "+owner+"."+name+signature);return result;
    }
    Constant constant(int owner,int operand) const {
        if(owner<0||size_t(owner)>=classes.size()||operand<0)throw Error("Invalid JPC constant reference");
        const size_t slot=size_t(classes[size_t(owner)].pool)+size_t(operand);
        if(slot>=indexCount)throw Error("JPC constant index outside table");
        const int index=s16(indexOffset+2*slot),absolute=index<0?-index:index;
        if(size_t(absolute)>=valueCount)throw Error("JPC constant value outside table");
        return {index,u32(valueOffset+4*size_t(absolute))};
    }
    struct Literal { std::vector<Value> chars;uint32_t offset=0,count=0; };
    Literal literal(uint32_t token) const {
        const size_t sizeOffset=valueOffset+4*valueCount;const size_t bytes=u32(sizeOffset),base=sizeOffset+4;
        range(base,bytes);
        auto header=[&](uint32_t ref,size_t payload) {
            if(ref<12||ref>bytes||payload>bytes-ref)throw Error("JPC literal reference outside heap");
            return std::tuple<uint32_t,uint32_t,uint32_t>{u32(base+ref-12),u32(base+ref-8),u32(base+ref-4)};
        };
        if(header(token,12)!=std::make_tuple(1u,4u,3u))throw Error("JPC literal is not a String");
        const auto chars=u32(base+token),offset=u32(base+token+4),count=u32(base+token+8);
        const auto [kind,width,length]=header(chars,0);
        if(kind||width!=1||length>1000000||length>bytes-chars||offset>length||count>length-offset)
            throw Error("Invalid JPC literal char array");
        Literal result;result.offset=offset;result.count=count;
        for(uint32_t i=0;i<length;++i)result.chars.push_back(Value::integer(data[base+chars+i]));
        return result;
    }
};

class Interpreter {
public:
    using ThreadID=uint32_t;
    enum class ThreadStatus { Waiting,Ready,Complete,Held };
    enum class WaitKind { Event,Frame };
    struct Wait {WaitKind kind=WaitKind::Event;int32_t actorType=0,actorIndex=0,event=0;uint64_t frame=0;};
    using Fields=std::map<uint32_t,Value>;
    using Native=std::function<Value(Interpreter&,Ref,const std::vector<Value>&)>;
    using Key=std::tuple<std::string,std::string,std::string>;
    Fields statics;
    std::vector<std::pair<int,uint32_t>> trace;
    uint64_t steps=0;
    const Package& package;
private:
    struct Entry { int classIndex=-1;char arrayKind=0;Fields fields;std::vector<Value> values; };
    struct Instruction { uint32_t pc=0,end=0;uint8_t op=0;int32_t a=0,b=0;std::vector<std::pair<int32_t,int32_t>> cases; };
    struct Code { std::vector<Instruction> instructions;std::map<uint32_t,size_t> positions; };
    struct Frame {
        int method=0;Ref receiver=0;std::vector<Value> args,locals,stack;
        size_t cursor=0;std::string pendingResult;
    };
    struct Suspend {Wait wait;std::vector<Frame> frames;};
    struct Thread {
        ThreadStatus status=ThreadStatus::Ready,heldStatus=ThreadStatus::Ready;
        Wait wait;std::vector<Frame> frames;Value result;
        int initialMethod=-1;Ref initialReceiver=0;std::vector<Value> initialArgs;
        uint32_t flags=1;bool detached=false;
    };
    std::deque<Entry> heap;
    std::map<Key,Native> natives;
    std::map<int,Code> decoded;
    std::map<uint32_t,Ref> strings;
    std::set<int> initialized;
    uint64_t stepLimit=100000,cells=0;
    unsigned depth=0;
    unsigned parkedDepth=0;
    unsigned callbackDepth=0;
    unsigned physicalDepth=0;
    ThreadID nextThread=1,runningThread=0;
    uint64_t sourceFrame=0;
    std::map<ThreadID,Thread> threads;
    Entry& entry(Ref ref) {
        if(!ref||ref>heap.size())throw Error("Null or invalid heap reference");return heap[size_t(ref)-1];
    }
    const Entry& entry(Ref ref) const {
        if(!ref||ref>heap.size())throw Error("Null or invalid heap reference");return heap[size_t(ref)-1];
    }
    void reserveCells(size_t count) {
        if(count>4000000-cells)throw Error("Script heap cell budget exceeded");cells+=count;
    }
    Ref allocate(Entry value) {
        if(heap.size()>=100000)throw Error("Script heap object budget exceeded");
        reserveCells(value.fields.size()+value.values.size());heap.push_back(std::move(value));return Ref(heap.size());
    }
    bool assignable(int actual,int expected) const {
        for(size_t count=0;actual>=0&&count<=package.classes.size();++count) {
            if(size_t(actual)>=package.classes.size())throw Error("Invalid script class");
            if(actual==expected)return true;actual=package.classes[size_t(actual)].parent;
        }
        return false;
    }
    Value typed(Value value,const std::string& kind) const {
        if(kind.size()==1&&std::string("ZBCSI").find(kind[0])!=std::string::npos) { value.integer();return value; }
        if(!kind.empty()&&(kind[0]=='L'||kind[0]=='[')) {
            if(value.null())return Value::reference(0);
            entry(value.reference());return value;
        }
        throw Error("Unsupported execution type: "+kind);
    }
    int virtualMethod(int index,Ref receiver) const {
        const auto& declaration=package.methods.at(size_t(index));
        const auto& object=entry(receiver);
        if(object.arrayKind||!assignable(object.classIndex,declaration.owner))throw Error("Invalid virtual receiver");
        int owner=object.classIndex;
        while(owner>=0) {
            int match=-1;
            for(size_t i=0;i<package.methods.size();++i) {
                const auto& m=package.methods[i];
                if(m.owner==owner&&m.name==declaration.name&&m.signature==declaration.signature&&!(m.flags&8)) {
                    if(match>=0)throw Error("Ambiguous virtual method");match=int(i);
                }
            }
            if(match>=0)return match;owner=package.classes[size_t(owner)].parent;
        }
        throw Error("Missing virtual method implementation");
    }
    const Code& code(int index) {
        auto cached=decoded.find(index);if(cached!=decoded.end())return cached->second;
        const auto& m=package.methods.at(size_t(index));Code result;uint32_t cursor=0;
        auto byte=[&]() { if(cursor>=m.length)throw Error("Truncated bytecode instruction");return package.data[56ull+m.offset+cursor++]; };
        auto u16=[&]() { uint32_t a=byte();return (a<<8)|byte(); };
        auto u32=[&]() { uint32_t a=u16();return (a<<16)|u16(); };
        auto s16=[&]() { auto a=u16();return a<32768?int32_t(a):int32_t(a)-65536; };
        while(cursor<m.length) {
            Instruction i;i.pc=cursor;i.op=byte();
            const auto op=i.op;
            if(op==0xaa||op==0xab) {
                while(cursor%4)byte();i.a=signedBits(u32());
                int32_t low=0,count=0;
                if(op==0xaa) {
                    low=signedBits(u32());const auto high=signedBits(u32());
                    const int64_t amount=int64_t(high)-low+1;
                    if(amount<1||amount>int64_t((m.length-cursor)/4))throw Error("Invalid tableswitch range");count=int32_t(amount);
                } else {
                    count=signedBits(u32());if(count<0||uint32_t(count)>(m.length-cursor)/8)throw Error("Invalid lookupswitch count");
                }
                for(int32_t n=0;n<count;++n) {
                    const int32_t key=op==0xaa?int32_t(int64_t(low)+n):signedBits(u32());
                    const int32_t delta=signedBits(u32());
                    if(op==0xab&&!i.cases.empty()&&key<=i.cases.back().first)throw Error("Unsorted lookupswitch keys");
                    i.cases.emplace_back(key,delta);
                }
            } else if(op==0xc4) {
                i.a=byte();i.b=int32_t(u16());
                if(i.a==0x84)i.cases.emplace_back(s16(),0);
                else if(i.a!=0x15&&i.a!=0x19&&i.a!=0x36&&i.a!=0x3a)throw Error("Unsupported wide instruction");
            } else if(op==0x10)i.a=int8_t(byte());
            else if(op==0x11||(op>=0x99&&op<=0xa8)||op==0xc6||op==0xc7)i.a=s16();
            else if(op==0x12||op==0xbc||op==0xa9||(op>=0x15&&op<=0x19)||(op>=0x36&&op<=0x3a))i.a=byte();
            else if(op==0x84) { i.a=byte();i.b=int8_t(byte()); }
            else if(op==0xc8||op==0xc9)i.a=signedBits(u32());
            else if(op==0x13||op==0x14||(op>=0xb2&&op<=0xb8)||op==0xbb||op==0xbd||op==0xc0||op==0xc1)i.a=int32_t(u16());
            else if(op==0xb9||op==0xba) { i.a=int32_t(u16());i.b=int32_t(u16()); }
            else if(op==0xc5) { i.a=int32_t(u16());i.b=byte(); }
            else if(op>0xc9)throw Error("Unknown bytecode opcode");
            i.end=cursor;result.positions.emplace(i.pc,result.instructions.size());result.instructions.push_back(std::move(i));
        }
        return decoded.emplace(index,std::move(result)).first->second;
    }
    Value call(int index,Ref receiver,const std::vector<Value>& arguments,const Frame* restored=nullptr) {
        if(index<0||size_t(index)>=package.methods.size())throw Error("Invalid method index");
        const auto& m=package.methods[size_t(index)];const auto& owner=package.classes[size_t(m.owner)].name;
        const std::string label=owner+"."+m.name+m.signature;int64_t pc=-1;
        if(depth+parkedDepth>=64||physicalDepth>=64)throw Error(label+": script call depth exceeded");
        ++depth;++physicalDepth;
        struct Unwind { unsigned& depth;unsigned& physical;~Unwind(){--depth;--physical;} } unwind{depth,physicalDepth};
        Frame frame;frame.method=index;frame.receiver=receiver;frame.args=arguments;bool hasFrame=false;
        try {
            const auto types=descriptor(m.signature);if(arguments.size()!=types.args.size())throw Error("Wrong argument count");
            std::vector<Value> args;
            for(size_t n=0;n<arguments.size();++n)args.push_back(typed(arguments[n],types.args[n]));
            const bool isStatic=(m.flags&8)!=0;
            if(isStatic&&receiver)throw Error("Static call has a receiver");
            if(!isStatic) { const auto& object=entry(receiver);if(object.arrayKind||!assignable(object.classIndex,m.owner))throw Error("Invalid instance receiver"); }
            if(owner=="ed/base/External"&&((m.name=="threadSleepEvent"&&m.signature=="(III)V")||
                (m.name=="threadSleepFrame"&&m.signature=="(I)V"))) {
                if(!runningThread)throw Error("Script suspension requires start(), not invoke()");
                Wait wait;
                if(m.name=="threadSleepEvent") {wait.actorType=args[0].integer();wait.actorIndex=args[1].integer();wait.event=args[2].integer();}
                else {
                    const auto delay=args[0].integer();if(delay<0||uint64_t(delay)>=std::numeric_limits<uint64_t>::max()-sourceFrame)
                        throw Error("Invalid script frame wait");
                    wait.kind=WaitKind::Frame;wait.frame=sourceFrame+uint64_t(delay)+1;
                }
                throw Suspend{wait,{}};
            }
            if(owner=="ed/base/External"&&m.signature=="(I)V"&&
               (m.name=="threadSuspendOther"||m.name=="threadAwakeOther")) {
                const auto id=args[0].integer();if(id<=0)throw Error("Invalid script thread ID");
                if(m.name=="threadSuspendOther")hold(ThreadID(id));else awake(ThreadID(id));return {};
            }
            auto native=natives.find({owner,m.name,m.signature});
            if(native!=natives.end()) {
                Value value=native->second(*this,receiver,args);
                if(types.result=="V") { if(value.kind!=Value::Kind::Void)throw Error("Void native returned a value");return {}; }
                return typed(value,types.result);
            }
            if(owner=="ed/base/External"||!m.hasCode)throw Error("Unbound native method");
            if(m.maxLocals<0||m.maxStack<0||m.maxLocals>65536||m.maxStack>65536)throw Error("Invalid frame limits");
            if(restored)frame=*restored;
            else frame.locals.assign(size_t(m.maxLocals),Value::unset());
            auto& locals=frame.locals;auto& stack=frame.stack;
            std::vector<Value> initial;if(!isStatic)initial.push_back(Value::reference(receiver));initial.insert(initial.end(),args.begin(),args.end());
            if(initial.size()>locals.size())throw Error("Arguments exceed local frame");
            if(!restored)std::copy(initial.begin(),initial.end(),locals.begin());
            auto pop=[&]() { if(stack.empty())throw Error("Operand stack underflow");auto v=stack.back();stack.pop_back();return v; };
            auto local=[&](int slot) { if(slot<0||size_t(slot)>=locals.size()||locals[size_t(slot)].kind==Value::Kind::Unset)throw Error("Invalid or uninitialized local");return locals[size_t(slot)]; };
            auto store=[&](int slot,Value value) { if(slot<0||size_t(slot)>=locals.size())throw Error("Local outside frame");locals[size_t(slot)]=value; };
            const auto& instructions=code(index);auto& cursor=frame.cursor;hasFrame=true;
            while(cursor<instructions.instructions.size()) {
                const auto& i=instructions.instructions[cursor++];pc=i.pc;const auto op=i.op;
                if(++steps>stepLimit)throw Error("Instruction budget exceeded");trace.emplace_back(index,i.pc);
                int64_t target=std::numeric_limits<int64_t>::min();
                auto cp=[&]() { return package.constant(m.owner,i.a); };
                if(op==0x00) {}
                else if(op==0x01)stack.push_back(Value::reference(0));
                else if(op>=0x02&&op<=0x08)stack.push_back(Value::integer(int(op)-3));
                else if(op==0x10||op==0x11)stack.push_back(Value::integer(i.a));
                else if(op==0x12||op==0x13) {
                    const auto c=cp();
                    if(c.index<0) {
                        auto existing=strings.find(c.bits);Ref ref=0;
                        if(existing!=strings.end())ref=existing->second;
                        else {
                            const auto literal=package.literal(c.bits);const Ref chars=makeArray('C',literal.chars);
                            ref=makeObject(package.classIndex("java/lang/String"),{{0,Value::reference(chars)},
                                {1,Value::integer(signedBits(literal.offset))},{2,Value::integer(signedBits(literal.count))}});
                            strings.emplace(c.bits,ref);
                        }
                        stack.push_back(Value::reference(ref));
                    } else stack.push_back(Value::integer(signedBits(c.bits)));
                } else if(op==0x15||op==0x19)stack.push_back(local(i.a));
                else if(op>=0x1a&&op<=0x1d)stack.push_back(typed(local(op-0x1a),"I"));
                else if(op>=0x2a&&op<=0x2d)stack.push_back(local(op-0x2a));
                else if(op==0x36||op==0x3a)store(i.a,pop());
                else if(op>=0x3b&&op<=0x3e)store(op-0x3b,typed(pop(),"I"));
                else if(op>=0x4b&&op<=0x4e)store(op-0x4b,pop());
                else if(op==0x2e||op==0x32||op==0x33||op==0x34||op==0x35||op==0x4f||op==0x53||op==0x54||op==0x55||op==0x56) {
                    const bool write=op>=0x4f;Value value=write?pop():Value{};const int32_t slot=pop().integer();auto& a=entry(pop().reference());
                    if(!a.arrayKind||slot<0||size_t(slot)>=a.values.size())throw Error("Array index outside bounds");
                    const char kind=(op==0x2e||op==0x4f)?'I':(op==0x32||op==0x53)?'L':(op==0x33||op==0x54)?'B':(op==0x34||op==0x55)?'C':'S';
                    if(a.arrayKind!=kind&&!(kind=='B'&&a.arrayKind=='Z'))throw Error("Array instruction type mismatch");
                    if(write) {
                        if(kind=='L')value=typed(value,"Ljava/lang/Object;");
                        else {
                            uint32_t v=value.bits;value.integer();
                            if(a.arrayKind=='B')v=(v&0x80)?v|0xffffff00u:v&255u;
                            else if(a.arrayKind=='S')v=(v&0x8000)?v|0xffff0000u:v&65535u;
                            else if(a.arrayKind=='C')v&=65535;else if(a.arrayKind=='Z')v&=1;
                            value=Value::integer(signedBits(v));
                        }
                        a.values[size_t(slot)]=value;
                    } else stack.push_back(a.values[size_t(slot)]);
                } else if(op==0x57||op==0x58) { pop();if(op==0x58)pop(); }
                else if(op==0x59||op==0x5a||op==0x5b||op==0x5c||op==0x5f) {
                    Value a=pop();if(op==0x59) {stack.push_back(a);stack.push_back(a);}
                    else { Value b=pop();if(op==0x5a)stack.insert(stack.end(),{a,b,a});
                        else if(op==0x5b) { Value c=pop();stack.insert(stack.end(),{a,c,b,a}); }
                        else if(op==0x5c)stack.insert(stack.end(),{b,a,b,a});else stack.insert(stack.end(),{a,b}); }
                } else if(op==0x60||op==0x64||op==0x68||op==0x6c||op==0x70||op==0x78||op==0x7a||op==0x7c||op==0x7e||op==0x80||op==0x82) {
                    const int32_t b=pop().integer(),a=pop().integer();const uint32_t ua=uint32_t(a),ub=uint32_t(b),shift=ub&31;uint32_t v=0;
                    if(op==0x60)v=ua+ub;else if(op==0x64)v=ua-ub;else if(op==0x68)v=ua*ub;
                    else if(op==0x6c||op==0x70) { if(!b)throw Error("Integer division by zero");const int64_t q=int64_t(a)/b;v=uint32_t(op==0x6c?q:int64_t(a)-q*b); }
                    else if(op==0x78)v=ua<<shift;else if(op==0x7c)v=ua>>shift;
                    else if(op==0x7a)v=(ua>>shift)|((a<0&&shift)?(~uint32_t(0)<<(32-shift)):0);
                    else if(op==0x7e)v=ua&ub;else if(op==0x80)v=ua|ub;else v=ua^ub;
                    stack.push_back(Value::integer(signedBits(v)));
                } else if(op==0x74)stack.push_back(Value::integer(signedBits(0u-uint32_t(pop().integer()))));
                else if(op==0x84)store(i.a,Value::integer(signedBits(uint32_t(local(i.a).integer())+uint32_t(i.b))));
                else if(op==0x91||op==0x92||op==0x93) {
                    uint32_t value=uint32_t(pop().integer());if(op==0x91)value=(value&128)?value|0xffffff00u:value&255u;
                    else if(op==0x92)value&=65535;else value=(value&32768)?value|0xffff0000u:value&65535u;
                    stack.push_back(Value::integer(signedBits(value)));
                } else if(op>=0x99&&op<=0xa6) {
                    bool match=false;
                    if(op>=0xa5) {Value b=pop(),a=pop();match=(a==b)==(op==0xa5);}
                    else {
                        int32_t a=0,b=0;int condition=0;
                        if(op<=0x9e) { a=pop().integer();condition=op-0x99; }
                        else {b=pop().integer();a=pop().integer();condition=op-0x9f;}
                        switch(condition) { case 0:match=a==b;break;case 1:match=a!=b;break;case 2:match=a<b;break;
                            case 3:match=a>=b;break;case 4:match=a>b;break;case 5:match=a<=b;break; }
                    }
                    if(match)target=pc+i.a;
                } else if(op==0xa7||op==0xc8)target=pc+i.a;
                else if(op==0xaa||op==0xab) {const auto key=pop().integer();target=pc+i.a;for(const auto& [value,delta]:i.cases)if(value==key){target=pc+delta;break;} }
                else if(op==0xac||op==0xb0||op==0xb1) {
                    if(op==0xb1) {if(types.result!="V")throw Error("Void return in value method");return {};}
                    if(types.result=="V")throw Error("Value return in void method");return typed(pop(),types.result);
                } else if(op>=0xb2&&op<=0xb5) {
                    const uint32_t field=cp().bits;if(field>=package.fieldCount)throw Error("Invalid field index");
                    const bool write=op==0xb3||op==0xb5;Value value=write?pop():Value{};
                    Ref object=0;if(op>=0xb4)object=pop().reference();
                    if(write) {if(object)setField(object,field,value);else if(op>=0xb4)throw Error("Null field receiver");else statics[field]=value;}
                    else stack.push_back(object?this->field(object,field):op>=0xb4?throw Error("Null field receiver"):staticField(field));
                } else if(op>=0xb6&&op<=0xb9) {
                    const uint32_t called=cp().bits;if(called>=package.methods.size())throw Error("Invalid called method");
                    const auto callTypes=descriptor(package.methods[called].signature);std::vector<Value> values(callTypes.args.size());
                    for(size_t n=values.size();n>0;--n)values[n-1]=pop();Ref object=op==0xb8?0:pop().reference();
                    const int actual=(op==0xb6||op==0xb9)?virtualMethod(int(called),object):int(called);
                    frame.pendingResult=callTypes.result;
                    const Value returned=call(actual,object,values);frame.pendingResult.clear();if(callTypes.result!="V")stack.push_back(returned);
                } else if(op==0xbb)stack.push_back(Value::reference(makeObject(int(cp().bits))));
                else if(op==0xbc||op==0xbd) {
                    const int32_t count=pop().integer();if(count<0||count>1000000)throw Error("Invalid or excessive array length");
                    const char kind=op==0xbd?'L':i.a==4?'Z':i.a==5?'C':i.a==8?'B':i.a==9?'S':i.a==10?'I':0;
                    if(!kind)throw Error("Unsupported primitive array type");
                    if(op==0xbd&&cp().bits>=package.classes.size())throw Error("Invalid array element class");
                    stack.push_back(Value::reference(makeArray(kind,std::vector<Value>(size_t(count),kind=='L'?Value::reference(0):Value::integer(0)))));
                } else if(op==0xbe)stack.push_back(Value::integer(int32_t(arrayValues(pop().reference()).size())));
                else if(op==0xc0||op==0xc1) {
                    const auto value=pop();bool matches=false;const uint32_t expected=cp().bits;
                    if(expected>=package.classes.size())throw Error("Invalid cast class");
                    if(!value.null()) {
                        const auto& object=entry(value.reference());
                        matches=object.arrayKind?package.classes[expected].name=="java/lang/Object":assignable(object.classIndex,int(expected));
                    }
                    if(op==0xc1)stack.push_back(Value::integer(matches?1:0));
                    else if(value.null()||matches)stack.push_back(value);else throw Error("Script class cast failed");
                } else if(op==0xc4) {
                    if(i.a==0x15||i.a==0x19)stack.push_back(local(i.b));
                    else if(i.a==0x36||i.a==0x3a)store(i.b,pop());
                    else store(i.b,Value::integer(signedBits(uint32_t(local(i.b).integer())+uint32_t(i.cases[0].first))));
                } else if(op==0xc6||op==0xc7) {if(pop().null()==(op==0xc6))target=pc+i.a;}
                else throw Error("Unsupported opcode "+std::to_string(op));
                if(stack.size()>size_t(m.maxStack))throw Error("Operand stack exceeds frame");
                if(target!=std::numeric_limits<int64_t>::min()) {
                    if(target<0||target>std::numeric_limits<uint32_t>::max())throw Error("Invalid branch destination");
                    auto destination=instructions.positions.find(uint32_t(target));if(destination==instructions.positions.end())throw Error("Invalid branch destination");cursor=destination->second;
                }
            }
            throw Error("Method fell through without return");
        } catch(Suspend& suspended) {if(hasFrame)suspended.frames.push_back(std::move(frame));throw;}
        catch(const Error& error) { throw Error(label+" @ "+std::to_string(pc)+": "+error.what()); }
        catch(const std::exception& error) { throw Error(label+" @ "+std::to_string(pc)+": invalid script operation: "+error.what()); }
    }
    void executeThread(ThreadID id) {
        if(depth||runningThread)throw Error("Cannot recursively resume a script thread");
        auto at=threads.find(id);if(at==threads.end())throw Error("Invalid script thread");
        Thread& thread=at->second;
        if(thread.status!=ThreadStatus::Ready)throw Error("Script thread is not ready");
        runningThread=id;steps=0;trace.clear();
        struct Running {ThreadID& id;unsigned& parked;~Running(){id=0;parked=0;}} running{runningThread,parkedDepth};
        auto saved=std::move(thread.frames);thread.frames.clear();size_t cursor=0;
        const int initialMethod=thread.initialMethod;const auto receiver=thread.initialReceiver;
        auto args=std::move(thread.initialArgs);thread.initialArgs.clear();thread.initialMethod=-1;thread.initialReceiver=0;
        try {
            Value returned;
            if(initialMethod>=0)returned=call(initialMethod,receiver,args);
            else for(;cursor<saved.size();++cursor) {
                Frame frame=std::move(saved[cursor]);parkedDepth=unsigned(saved.size()-cursor-1);
                if(cursor&&frame.pendingResult!="V")frame.stack.push_back(typed(returned,frame.pendingResult));
                frame.pendingResult.clear();returned=call(frame.method,frame.receiver,frame.args,&frame);
            }
            thread.status=ThreadStatus::Complete;thread.result=returned;
        } catch(Suspend& suspended) {
            if(initialMethod<0)for(size_t i=cursor+1;i<saved.size();++i)suspended.frames.push_back(std::move(saved[i]));
            if(suspended.frames.size()>64)throw Error("Suspended script frame depth exceeded");
            thread.frames=std::move(suspended.frames);thread.wait=suspended.wait;thread.status=ThreadStatus::Waiting;
        } catch(...) {threads.erase(id);throw;}
    }
public:
    explicit Interpreter(const Package& source,uint64_t limit=100000):package(source),stepLimit(limit) {
        if(!limit)throw Error("Invalid instruction budget");
    }
    void bind(const std::string& owner,const std::string& name,const std::string& signature,Native native) {
        package.methodIndex(owner,name,signature);if(!native)throw Error("Empty native binding");natives[{owner,name,signature}]=std::move(native);
    }
    Ref makeObject(int owner,Fields fields={}) {
        if(owner<0||size_t(owner)>=package.classes.size())throw Error("Invalid heap object class");
        for(const auto& [index,value]:fields) { (void)value;if(index>=package.fieldCount)throw Error("Invalid object field"); }
        return allocate({owner,0,std::move(fields),{}});
    }
    Ref makeArray(char kind,std::vector<Value> values) {
        if(std::string("LZBCSI").find(kind)==std::string::npos)throw Error("Unsupported array type");
        if(values.size()>1000000)throw Error("Excessive array length");
        for(auto& v:values)v=typed(v,kind=='L'?"Ljava/lang/Object;":"I");
        return allocate({-1,kind,{},std::move(values)});
    }
    Value staticField(uint32_t index) const {
        if(index>=package.fieldCount)throw Error("Invalid static field");auto at=statics.find(index);return at==statics.end()?Value::integer(0):at->second;
    }
    Value field(Ref ref,uint32_t index) const {
        if(index>=package.fieldCount)throw Error("Invalid object field");const auto& object=entry(ref);if(object.arrayKind)throw Error("Array has no object fields");
        auto at=object.fields.find(index);return at==object.fields.end()?Value::integer(0):at->second;
    }
    void setField(Ref ref,uint32_t index,Value value) {
        if(index>=package.fieldCount)throw Error("Invalid object field");auto& object=entry(ref);if(object.arrayKind)throw Error("Array has no object fields");
        if(!object.fields.count(index))reserveCells(1);object.fields[index]=value;
    }
    std::vector<Value>& arrayValues(Ref ref) {auto& array=entry(ref);if(!array.arrayKind)throw Error("Object is not an array");return array.values;}
    const std::vector<Value>& arrayValues(Ref ref) const {const auto& array=entry(ref);if(!array.arrayKind)throw Error("Object is not an array");return array.values;}
    int objectClass(Ref ref) const {const auto& object=entry(ref);if(object.arrayKind)throw Error("Array has no class");return object.classIndex;}
    Interpreter cloneState() const {
        if(depth||runningThread)throw Error("Cannot checkpoint an executing script frame");return *this;
    }
    void restoreFrom(const Interpreter& source) {
        if(depth||runningThread||source.depth||source.runningThread||&package!=&source.package)throw Error("Invalid script transaction checkpoint");
        heap=source.heap;statics=source.statics;strings=source.strings;initialized=source.initialized;cells=source.cells;
        threads=source.threads;nextThread=source.nextThread;sourceFrame=source.sourceFrame;steps=source.steps;trace=source.trace;
    }
    std::vector<uint8_t> snapshot() const {
        if(depth||runningThread)throw Error("Cannot snapshot an executing script frame");
        const auto hash=[](const std::vector<uint8_t>& bytes,size_t count) {
            uint32_t value=2166136261u;for(size_t i=0;i<count;++i)value=(value^bytes[i])*16777619u;return value;
        };
        std::vector<uint8_t> out={'E','D','V','M','S','T','3',0};
        auto put=[&](uint32_t value){for(unsigned b=0;b<4;++b)out.push_back(uint8_t(value>>(b*8)));};
        auto wide=[&](uint64_t value){put(uint32_t(value));put(uint32_t(value>>32));};
        auto value=[&](Value v,bool frame=false){
            if((!frame&&v.kind!=Value::Kind::Integer&&v.kind!=Value::Kind::Reference)||unsigned(v.kind)>3)
                throw Error("Invalid persistent script value");
            put(uint32_t(v.kind));put(v.bits);
        };
        auto fields=[&](const Fields& source){put(uint32_t(source.size()));for(const auto& [key,v]:source){put(key);value(v);}};
        auto values=[&](const std::vector<Value>& source){put(uint32_t(source.size()));for(auto v:source)value(v,true);};
        put(hash(package.data,package.data.size()));put(uint32_t(heap.size()));fields(statics);
        for(const auto& object:heap) {
            put(uint32_t(object.classIndex));put(uint32_t(object.arrayKind));fields(object.fields);
            put(uint32_t(object.values.size()));for(auto v:object.values)value(v);
        }
        put(uint32_t(strings.size()));for(const auto& [token,ref]:strings){put(token);put(ref);}
        put(uint32_t(initialized.size()));for(int owner:initialized)put(uint32_t(owner));
        wide(sourceFrame);put(nextThread);put(uint32_t(threads.size()));
        for(const auto& [id,thread]:threads) {
            put(id);put(uint32_t(thread.status));put(uint32_t(thread.heldStatus));put(uint32_t(thread.wait.kind));
            put(uint32_t(thread.wait.actorType));put(uint32_t(thread.wait.actorIndex));put(uint32_t(thread.wait.event));wide(thread.wait.frame);
            value(thread.result,true);put(uint32_t(thread.initialMethod));put(thread.initialReceiver);values(thread.initialArgs);
            put(uint32_t(thread.frames.size()));
            for(const auto& frame:thread.frames) {
                put(uint32_t(frame.method));put(frame.receiver);put(uint32_t(frame.cursor));
                put(uint32_t(frame.pendingResult.size()));out.insert(out.end(),frame.pendingResult.begin(),frame.pendingResult.end());
                values(frame.args);values(frame.locals);values(frame.stack);
            }
            put(thread.flags);put(thread.detached?1u:0u);
        }
        if(out.size()>128ull*1024*1024-4)throw Error("Excessive script snapshot size");
        put(hash(out,out.size()));return out;
    }
    void restore(const std::vector<uint8_t>& bytes) {
        if(depth||runningThread)throw Error("Cannot restore an executing script frame");
        if(bytes.size()<24||bytes.size()>128ull*1024*1024||
           (std::memcmp(bytes.data(),"EDVMST1\0",8)&&std::memcmp(bytes.data(),"EDVMST2\0",8)&&std::memcmp(bytes.data(),"EDVMST3\0",8)))
            throw Error("Invalid script snapshot header");
        const bool version2=bytes[6]>='2',version3=bytes[6]=='3';
        auto hash=[](const std::vector<uint8_t>& data,size_t count){
            uint32_t value=2166136261u;for(size_t i=0;i<count;++i)value=(value^data[i])*16777619u;return value;
        };
        size_t cursor=8;
        auto take=[&](){
            if(cursor>bytes.size()-4||bytes.size()-4-cursor<4)throw Error("Truncated script snapshot");
            uint32_t value=0;for(unsigned b=0;b<4;++b)value|=uint32_t(bytes[cursor++])<<(b*8);return value;
        };
        const size_t checksumOffset=bytes.size()-4;
        uint32_t checksum=0;for(unsigned b=0;b<4;++b)checksum|=uint32_t(bytes[checksumOffset+b])<<(b*8);
        if(hash(bytes,checksumOffset)!=checksum||take()!=hash(package.data,package.data.size()))
            throw Error("Script snapshot checksum or package mismatch");
        const uint32_t count=take();if(count>100000)throw Error("Excessive snapshot heap count");
        uint64_t newCells=0;
        auto reserve=[&](uint32_t amount){if(amount>4000000-newCells)throw Error("Snapshot heap budget exceeded");newCells+=amount;};
        auto value=[&](bool frame=false){
            const uint32_t kind=take(),bits=take();
            if(kind>(frame?3u:1u)||(kind==1&&bits>count)||(kind>1&&bits))throw Error("Invalid snapshot value or reference");
            return Value{Value::Kind(kind),bits};
        };
        auto fields=[&](bool heapFields){
            Fields result;const uint32_t size=take();
            if(size>package.fieldCount)throw Error("Excessive snapshot field count");
            if(heapFields)reserve(size);
            for(uint32_t i=0;i<size;++i){const uint32_t key=take();const Value v=value();
                if(key>=package.fieldCount||!result.emplace(key,v).second)throw Error("Invalid snapshot field");}
            return result;
        };
        Fields newStatics=fields(false);std::deque<Entry> newHeap;
        for(uint32_t i=0;i<count;++i) {
            Entry object;object.classIndex=signedBits(take());const uint32_t kind=take();
            if(kind>127)throw Error("Invalid snapshot array kind");object.arrayKind=char(kind);
            if((!kind&&(object.classIndex<0||size_t(object.classIndex)>=package.classes.size()))||
               (kind&&(object.classIndex!=-1||std::string("LZBCSI").find(char(kind))==std::string::npos)))
                throw Error("Invalid snapshot object type");
            object.fields=fields(true);const uint32_t size=take();
            if(size>1000000||(!kind&&size)||(kind&&!object.fields.empty()))throw Error("Invalid snapshot array layout");
            reserve(size);
            for(uint32_t n=0;n<size;++n){Value v=value();
                if(kind=='L'){if(v.null())v=Value::reference(0);else if(v.kind!=Value::Kind::Reference)throw Error("Invalid reference array snapshot");}
                else if(v.kind!=Value::Kind::Integer)throw Error("Invalid primitive array snapshot");
                object.values.push_back(v);}
            newHeap.push_back(std::move(object));
        }
        std::map<uint32_t,Ref> newStrings;const uint32_t stringCount=take();
        if(stringCount>count)throw Error("Excessive snapshot string count");
        for(uint32_t i=0;i<stringCount;++i){const auto token=take(),ref=take();
            if(!ref||ref>count||newHeap[size_t(ref)-1].classIndex!=package.classIndex("java/lang/String")||
               !newStrings.emplace(token,ref).second)throw Error("Invalid snapshot string reference");
            package.literal(token);
        }
        std::set<int> newInitialized;const uint32_t classCount=take();
        if(classCount>package.classes.size())throw Error("Excessive snapshot initializer count");
        for(uint32_t i=0;i<classCount;++i){const uint32_t owner=take();
            if(owner>=package.classes.size()||!newInitialized.insert(int(owner)).second)throw Error("Invalid snapshot initializer");}
        uint64_t newFrame=0,schedulerCells=0;ThreadID newNext=1;
        std::map<ThreadID,Thread> newThreads;
        if(version2) {
            auto wide=[&](){const uint64_t low=take(),high=take();return low|(high<<32);};
            auto values=[&](uint32_t maximum,bool unset=false) {
                const uint32_t amount=take();if(amount>maximum||amount>4000000-schedulerCells)throw Error("Snapshot frame cell budget exceeded");
                schedulerCells+=amount;std::vector<Value> result;result.reserve(amount);
                for(uint32_t n=0;n<amount;++n){auto v=value(true);
                    if(v.kind==Value::Kind::Void||(!unset&&v.kind==Value::Kind::Unset))throw Error("Invalid snapshot frame value");result.push_back(v);}
                return result;
            };
            auto typedSaved=[&](Value v,const std::string& kind) {
                if(kind.size()==1&&std::string("ZBCSI").find(kind[0])!=std::string::npos) {if(v.kind!=Value::Kind::Integer)throw Error("Invalid snapshot integer argument");}
                else if(!kind.empty()&&(kind[0]=='L'||kind[0]=='[')) {if(!v.null()&&v.kind!=Value::Kind::Reference)throw Error("Invalid snapshot reference argument");}
                else throw Error("Unsupported snapshot execution type");
            };
            auto invocation=[&](int method,Ref receiver,const std::vector<Value>& args) {
                if(method<0||size_t(method)>=package.methods.size())throw Error("Invalid snapshot method");
                const auto& m=package.methods[size_t(method)];const auto types=descriptor(m.signature);
                if(args.size()!=types.args.size())throw Error("Invalid snapshot argument count");
                for(size_t n=0;n<args.size();++n)typedSaved(args[n],types.args[n]);
                if(m.flags&8) {if(receiver)throw Error("Invalid snapshot static receiver");}
                else if(!receiver||receiver>count||newHeap[size_t(receiver)-1].arrayKind||
                        !assignable(newHeap[size_t(receiver)-1].classIndex,m.owner))throw Error("Invalid snapshot instance receiver");
            };
            newFrame=wide();newNext=take();const uint32_t amount=take();
            if(amount>1024)throw Error("Snapshot thread budget exceeded");
            for(uint32_t n=0;n<amount;++n) {
                const ThreadID id=take();Thread thread;const auto status=take(),held=take(),kind=take();
                if(!id||(newNext&&id>=newNext)||status>uint32_t(ThreadStatus::Held)||
                   (held!=uint32_t(ThreadStatus::Ready)&&held!=uint32_t(ThreadStatus::Waiting))||kind>uint32_t(WaitKind::Frame))
                    throw Error("Invalid snapshot thread state");
                thread.status=ThreadStatus(status);thread.heldStatus=ThreadStatus(held);thread.wait.kind=WaitKind(kind);
                thread.wait.actorType=signedBits(take());thread.wait.actorIndex=signedBits(take());thread.wait.event=signedBits(take());thread.wait.frame=wide();
                thread.result=value(true);thread.initialMethod=signedBits(take());thread.initialReceiver=take();thread.initialArgs=values(65536);
                const uint32_t frames=take();if(frames>64)throw Error("Snapshot frame depth exceeded");
                for(uint32_t f=0;f<frames;++f) {
                    Frame frame;frame.method=signedBits(take());frame.receiver=take();frame.cursor=take();
                    const uint32_t length=take();if(length>1024||cursor>checksumOffset||length>checksumOffset-cursor)throw Error("Invalid snapshot result descriptor");
                    frame.pendingResult.assign(reinterpret_cast<const char*>(bytes.data()+cursor),length);cursor+=length;
                    frame.args=values(65536);frame.locals=values(65536,true);frame.stack=values(65536);
                    invocation(frame.method,frame.receiver,frame.args);const auto& method=package.methods[size_t(frame.method)];
                    if(package.classes[size_t(method.owner)].name=="ed/base/External"||!method.hasCode||method.maxLocals<0||method.maxStack<0||
                       frame.locals.size()!=size_t(method.maxLocals)||frame.stack.size()>size_t(method.maxStack))throw Error("Invalid snapshot frame layout");
                    const auto& instructions=code(frame.method).instructions;
                    if(!frame.cursor||frame.cursor>=instructions.size())throw Error("Invalid snapshot frame cursor");
                    const auto& previous=instructions[frame.cursor-1];
                    if(previous.op<0xb6||previous.op>0xb9)throw Error("Snapshot frame is not parked at a call");
                    const auto called=package.constant(method.owner,previous.a).bits;
                    if(called>=package.methods.size()||frame.pendingResult!=descriptor(package.methods[called].signature).result)
                        throw Error("Invalid snapshot pending result");
                    if(frame.stack.size()+(frame.pendingResult!="V"?1u:0u)>size_t(method.maxStack))throw Error("Invalid snapshot pending stack");
                    if(!f) {
                        const auto& leaf=package.methods[called];
                        if(package.classes[size_t(leaf.owner)].name!="ed/base/External"||
                           !((leaf.name=="threadSleepEvent"&&leaf.signature=="(III)V")||
                             (leaf.name=="threadSleepFrame"&&leaf.signature=="(I)V")))throw Error("Invalid snapshot suspension call");
                    } else {
                        const auto& child=thread.frames.back();const auto& declaration=package.methods[called];int expected=int(called);
                        if(previous.op==0xb6||previous.op==0xb9) {
                            if(!child.receiver||child.receiver>count||newHeap[size_t(child.receiver)-1].arrayKind||
                               !assignable(newHeap[size_t(child.receiver)-1].classIndex,declaration.owner))throw Error("Invalid snapshot virtual receiver");
                            int owner=newHeap[size_t(child.receiver)-1].classIndex;expected=-1;
                            while(owner>=0&&expected<0) {
                                for(size_t i=0;i<package.methods.size();++i) {
                                    const auto& candidate=package.methods[i];
                                    if(candidate.owner==owner&&candidate.name==declaration.name&&candidate.signature==declaration.signature&&!(candidate.flags&8)) {
                                        if(expected>=0)throw Error("Ambiguous snapshot virtual method");expected=int(i);
                                    }
                                }
                                owner=package.classes[size_t(owner)].parent;
                            }
                        }
                        if(child.method!=expected)
                            throw Error("Invalid snapshot caller chain");
                    }
                    thread.frames.push_back(std::move(frame));
                }
                if(version3) {
                    thread.flags=take();const uint32_t detached=take();
                    if(thread.flags>1||detached>1)throw Error("Invalid snapshot thread flags");thread.detached=detached!=0;
                }
                if(thread.initialMethod>=0) {
                    invocation(thread.initialMethod,thread.initialReceiver,thread.initialArgs);
                    if(frames||(thread.status!=ThreadStatus::Ready&&!(thread.status==ThreadStatus::Held&&thread.heldStatus==ThreadStatus::Ready)))
                        throw Error("Invalid snapshot queued thread");
                } else if(thread.initialMethod!=-1||thread.initialReceiver||!thread.initialArgs.empty())throw Error("Invalid snapshot initial frame");
                if(thread.result.kind==Value::Kind::Unset||(thread.status!=ThreadStatus::Complete&&thread.result.kind!=Value::Kind::Void)||
                   (thread.status==ThreadStatus::Complete&&(frames||thread.initialMethod>=0))||
                   ((thread.status==ThreadStatus::Waiting||(thread.status==ThreadStatus::Held&&thread.heldStatus==ThreadStatus::Waiting))&&
                    thread.wait.kind==WaitKind::Frame&&!thread.wait.frame))throw Error("Invalid snapshot continuation state");
                if(thread.detached&&(thread.result.kind!=Value::Kind::Void||
                   (thread.initialMethod>=0&&descriptor(package.methods[size_t(thread.initialMethod)].signature).result!="V")||
                   (!thread.frames.empty()&&descriptor(package.methods[size_t(thread.frames.back().method)].signature).result!="V")))
                    throw Error("Invalid snapshot callback ownership");
                if(!newThreads.emplace(id,std::move(thread)).second)throw Error("Duplicate snapshot thread ID");
            }
        }
        if(cursor!=checksumOffset)throw Error("Trailing script snapshot bytes");
        heap.swap(newHeap);statics.swap(newStatics);strings.swap(newStrings);initialized.swap(newInitialized);
        cells=newCells;steps=0;trace.clear();threads.swap(newThreads);nextThread=newNext;sourceFrame=newFrame;
    }
    ThreadID enqueue(int method,Ref receiver=0,std::vector<Value> args={},uint32_t flags=1) {
        if(flags>1)throw Error("Unsupported script thread flags");
        if(method<0||size_t(method)>=package.methods.size())throw Error("Invalid script method index");
        const auto& declaration=package.methods[size_t(method)];const auto types=descriptor(declaration.signature);
        if(args.size()!=types.args.size())throw Error("Wrong argument count");
        for(size_t n=0;n<args.size();++n)typed(args[n],types.args[n]);
        if(declaration.flags&8) {if(receiver)throw Error("Static call has a receiver");}
        else {const auto& object=entry(receiver);if(object.arrayKind||!assignable(object.classIndex,declaration.owner))throw Error("Invalid instance receiver");}
        if(threads.size()>=1024||!nextThread)throw Error("Script thread budget exceeded");
        Thread thread;thread.initialMethod=method;thread.initialReceiver=receiver;thread.initialArgs=std::move(args);thread.flags=flags;
        const ThreadID id=nextThread++;threads.emplace(id,std::move(thread));return id;
    }
    ThreadID start(int method,Ref receiver=0,std::vector<Value> args={},uint32_t flags=1) {
        if(depth||runningThread)throw Error("Cannot recursively start a script thread; use startCallback");
        const auto id=enqueue(method,receiver,std::move(args),flags);executeThread(id);return id;
    }
    // Native4331b0 starts UI callbacks immediately on an independent context;
    // their flags0 continuations remain runnable while gameplay flags1 is masked.
    ThreadID startCallback(int method,Ref receiver=0,std::vector<Value> args={},uint32_t flags=0) {
        if(method<0||size_t(method)>=package.methods.size()||descriptor(package.methods[size_t(method)].signature).result!="V")
            throw Error("Script callback must return void");
        if(callbackDepth>=16)throw Error("Script callback depth exceeded");
        if(!depth&&!runningThread){steps=0;trace.clear();}
        const auto id=enqueue(method,receiver,std::move(args),flags);threads.at(id).detached=true;
        const auto outerDepth=depth,outerParked=parkedDepth;const auto outerThread=runningThread;const auto outerSteps=steps;
        auto outerTrace=std::move(trace);depth=0;parkedDepth=0;runningThread=0;++callbackDepth;
        struct Context {
            Interpreter& vm;unsigned depth,parked;ThreadID thread;uint64_t steps;std::vector<std::pair<int,uint32_t>> trace;
            ~Context(){vm.depth=depth;vm.parkedDepth=parked;vm.runningThread=thread;vm.steps=steps;--vm.callbackDepth;
                trace.insert(trace.end(),vm.trace.begin(),vm.trace.end());vm.trace=std::move(trace);}
        } context{*this,outerDepth,outerParked,outerThread,outerSteps,std::move(outerTrace)};
        executeThread(id);
        if(status(id)==ThreadStatus::Complete){threads.erase(id);return 0;}return id;
    }
    ThreadStatus status(ThreadID id) const {auto at=threads.find(id);if(at==threads.end())throw Error("Invalid script thread");return at->second.status;}
    bool threadMatches(ThreadID id,uint32_t flags,bool detached=false) const {
        auto at=threads.find(id);if(at==threads.end())throw Error("Invalid script thread");return at->second.flags==flags&&at->second.detached==detached;
    }
    Wait wait(ThreadID id) const {auto at=threads.find(id);if(at==threads.end()||at->second.status!=ThreadStatus::Waiting)throw Error("Script thread is not waiting");return at->second.wait;}
    Value result(ThreadID id) const {auto at=threads.find(id);if(at==threads.end()||at->second.status!=ThreadStatus::Complete)throw Error("Script thread is not complete");return at->second.result;}
    void forget(ThreadID id) {if(status(id)!=ThreadStatus::Complete)throw Error("Cannot discard a suspended script thread");threads.erase(id);}
    size_t pendingThreads() const {size_t count=0;for(const auto& [id,thread]:threads){(void)id;if(thread.status!=ThreadStatus::Complete)++count;}return count;}
    void clearSceneThreads() {
        // Native432560 drops every active background slot1+ regardless of wait,
        // held or queued state. Synchronous main calls are outside this map.
        if(depth||runningThread)throw Error("Cannot clear scene threads while executing script");
        threads.clear(); // Heap/statics, sourceFrame and monotonically assigned IDs survive.
    }
    void hold(ThreadID id) {
        auto at=threads.find(id);if(at==threads.end()||id==runningThread||at->second.status==ThreadStatus::Complete)
            throw Error("Cannot hold invalid, executing or complete script thread");
        auto& thread=at->second;
        // Native433f7f saves an inactive wait mode; another hold of mode0 discards it.
        thread.heldStatus=thread.status==ThreadStatus::Waiting?ThreadStatus::Waiting:ThreadStatus::Ready;
        thread.status=ThreadStatus::Held;
    }
    void awake(ThreadID id) {
        auto at=threads.find(id);if(at==threads.end()||id==runningThread||at->second.status==ThreadStatus::Complete)
            throw Error("Cannot awake invalid, executing or complete script thread");
        auto& thread=at->second;
        thread.status=thread.status==ThreadStatus::Held?thread.heldStatus:ThreadStatus::Ready;
        if(thread.status==ThreadStatus::Waiting&&thread.wait.kind==WaitKind::Frame&&thread.wait.frame<=sourceFrame)
            thread.status=ThreadStatus::Ready;
        thread.heldStatus=ThreadStatus::Ready;
    }
    std::vector<ThreadID> notify(int32_t actorType,int32_t actorIndex,int32_t event) {
        std::vector<ThreadID> ready;
        for(auto& [id,thread]:threads)if((thread.status==ThreadStatus::Waiting||
            (thread.status==ThreadStatus::Held&&thread.heldStatus==ThreadStatus::Waiting))&&thread.wait.kind==WaitKind::Event&&
            (thread.wait.actorType<0||thread.wait.actorType==actorType)&&(thread.wait.actorIndex<0||thread.wait.actorIndex==actorIndex)&&thread.wait.event==event) {
            if(thread.status==ThreadStatus::Held)thread.heldStatus=ThreadStatus::Ready;
            else {thread.status=ThreadStatus::Ready;ready.push_back(id);}
        }
        return ready;
    }
    std::vector<ThreadID> advanceFrame(uint32_t mask=0) {
        if(sourceFrame==std::numeric_limits<uint64_t>::max())throw Error("Script frame counter overflow");++sourceFrame;
        std::vector<ThreadID> ready;
        for(auto& [id,thread]:threads)if(!(mask&thread.flags)&&thread.status==ThreadStatus::Waiting&&thread.wait.kind==WaitKind::Frame&&thread.wait.frame<=sourceFrame) {
            thread.status=ThreadStatus::Ready;ready.push_back(id);
        }
        return ready;
    }
    bool resume(ThreadID id) {executeThread(id);return status(id)==ThreadStatus::Complete;}
    std::vector<ThreadID> runReady(uint32_t mask=0) {
        std::vector<ThreadID> order,executed;for(const auto& [id,thread]:threads){(void)thread;order.push_back(id);}
        for(ThreadID id:order)if(status(id)==ThreadStatus::Ready&&!(mask&threads.at(id).flags)) {
            resume(id);executed.push_back(id);if(status(id)==ThreadStatus::Complete&&threads.at(id).detached)threads.erase(id);
        }return executed;
    }
    Value invoke(int method,Ref receiver=0,std::vector<Value> args={}) {
        if(!depth) {steps=0;trace.clear();}return call(method,receiver,args);
    }
    void initialize(const std::string& owner) {
        const int index=package.classIndex(owner);if(initialized.count(index))return;
        int method=-1;
        for(size_t n=0;n<package.methods.size();++n)if(package.methods[n].owner==index&&package.methods[n].name=="<clinit>") {
            if(method>=0)throw Error("Duplicate class initializer");method=int(n);
        }
        if(method>=0)invoke(method);initialized.insert(index);
    }
};
} // namespace jpc
