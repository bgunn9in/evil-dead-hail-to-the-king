#pragma once
#include "save_file.h"
#include <cmath>
#include <functional>
#include <optional>

namespace edcart {
enum class Operation {Access,Load,Save,Delete};
struct Preview {std::string room;double seconds=0;};
struct Slot {unsigned status=0;Preview preview;};
struct Request {Operation operation=Operation::Access;unsigned slot=0;};
struct Result {Request request;bool success=false;std::string diagnostic;};
struct State {
    std::array<Slot,8> slots;
    std::optional<Request> pending;
    std::optional<Result> completed;
};
struct Hooks {
    // Must fully validate the incoming World/VM and return its actual preview.
    std::function<Preview(const savefile::Bytes&)> validate;
    std::function<savefile::Bytes()> save;
    // Must retain the previous World on failure. Keep Adapter alive through
    // process(): perform an owning-pointer swap after this function returns.
    std::function<void(const savefile::Bytes&)> load;
};

class Adapter {
    savefile::fs::path directory_;
    size_t limit_;
    State state_;
    bool processing_=false;
    static void slotIndex(unsigned slot){if(slot>=8)throw savefile::Error("Cart slot is outside0..7");}
    static void operation(Operation op){if(unsigned(op)>unsigned(Operation::Delete))throw savefile::Error("Invalid Cart operation");}
    static void preview(const Preview& value) {
        if(value.room.empty()||value.room.size()>64||!std::isfinite(value.seconds)||value.seconds<0||value.seconds>1e12||
           std::any_of(value.room.begin(),value.room.end(),[](unsigned char c){return c<32||c>126;}))
            throw savefile::Error("Invalid validated save preview");
    }
    Preview validate(const Hooks& hooks,const savefile::Bytes& bytes) const {
        if(!hooks.validate)throw savefile::Error("Cart requires a full World validator");
        auto result=hooks.validate(bytes);preview(result);return result;
    }
    std::array<Slot,8> scan(const Hooks& hooks) const {
        if(!hooks.validate)throw savefile::Error("Cart requires a full World validator");
        savefile::detail::directory(directory_);
        std::array<Slot,8> slots;
        for(unsigned i=0;i<slots.size();++i) {
            try {const auto bytes=savefile::read(directory_,filename(i),limit_);slots[i]={2,validate(hooks,bytes)};}
            catch(const std::exception&) {slots[i]={};} // Original catalog marks missing/corrupt records0.
        }
        return slots;
    }
    void erase(unsigned slot) const {
        const auto directory=savefile::detail::directory(directory_);
        const auto target=directory/filename(slot);
        // Delete only this explicitly selected modern-envelope file. Never
        // follow a reparse point or delete a foreign original-game save.
        if(target.parent_path()!=directory||!savefile::detail::existing(target,limit_))throw savefile::Error("Cart save file does not exist");
        if(!savefile::fs::remove(target))throw savefile::Error("Cart delete did not remove the selected save");
    }
public:
    explicit Adapter(const savefile::fs::path& directory,size_t maximum=savefile::DefaultLimit):
        directory_(savefile::detail::directory(directory)),limit_(maximum) {savefile::detail::limit(maximum);}
    static std::string filename(unsigned slot) {slotIndex(slot);return slot?"slot"+std::to_string(slot)+".eds":"quicksave.eds";}
    static constexpr int present(){return 1;} // PC418C60.
    static constexpr int freeBlocks(){return 1024;} // PC41C8F0.
    static constexpr int countValidSlots(){return 1;} // Registered383 ->4083B0->418C60.
    static constexpr uint32_t cardTextResource(){return 0x31990960u;}
    int countValidCardSlots() const {return int(std::count_if(state_.slots.begin(),state_.slots.end(),[](const auto& slot){return slot.status==0;}));}
    int checkValidSlot(unsigned slot) const {slotIndex(slot);return int(state_.slots[slot].status);}
    const Slot& catalog(unsigned slot) const {slotIndex(slot);return state_.slots[slot];}
    bool pending() const {return state_.pending.has_value();}
    int done() const {return !pending()&&state_.completed.has_value()?1:0;}
    int error() const {return !pending()&&state_.completed&&!state_.completed->success?4:0;}
    int queue(Operation op,unsigned slot=0) {
        operation(op);slotIndex(slot);
        if(processing_||pending())return 0;
        state_.pending=Request{op,slot};state_.completed.reset();return 1;
    }
    std::optional<Result> process(const Hooks& hooks) {
        if(processing_)throw savefile::Error("Cart process is reentrant");
        if(!state_.pending)return {};
        const Request request=*state_.pending;Result result{request,false,{}};
        processing_=true;
        struct Processing {bool& value;~Processing(){value=false;}} processing{processing_};
        try {
            switch(request.operation) {
            case Operation::Access:state_.slots=scan(hooks);break;
            case Operation::Load: {
                savefile::Bytes bytes;Preview checked;
                try {bytes=savefile::read(directory_,filename(request.slot),limit_);checked=validate(hooks,bytes);}
                catch(...) {state_.slots[request.slot]={};throw;}
                state_.slots[request.slot]={2,std::move(checked)};
                if(!hooks.load)throw savefile::Error("Cart requires a transactional World loader");
                hooks.load(bytes);break;
            }
            case Operation::Save: {
                if(!hooks.save)throw savefile::Error("Cart requires a World serializer");
                const auto bytes=hooks.save();
                if(bytes.size()>limit_)throw savefile::Error("Cart save exceeds its byte limit");
                auto checked=validate(hooks,bytes);
                savefile::atomicWrite(directory_,filename(request.slot),bytes,limit_);
                state_.slots[request.slot]={2,std::move(checked)};break;
            }
            case Operation::Delete:erase(request.slot);state_.slots[request.slot]={};break;
            }
            result.success=true;
        } catch(const std::exception& exception) {result.diagnostic=exception.what();if(result.diagnostic.size()>1024)result.diagnostic.resize(1024);}
        state_.pending.reset();state_.completed=result;return result;
    }
    // State is an in-memory transaction checkpoint, not a trusted on-disk
    // catalog. On opening the source save UI, Access revalidates actual files.
    State checkpoint() const {if(processing_)throw savefile::Error("Cannot checkpoint a processing Cart request");return state_;}
    void restore(const State& state) {
        if(processing_)throw savefile::Error("Cannot restore a processing Cart request");
        for(const auto& slot:state.slots) {
            if(slot.status!=0&&slot.status!=2)throw savefile::Error("Invalid Cart catalog state");
            if(slot.status==2)preview(slot.preview);
            else if(!slot.preview.room.empty()||slot.preview.seconds!=0)throw savefile::Error("Invalid empty Cart preview");
        }
        if(state.pending){operation(state.pending->operation);slotIndex(state.pending->slot);}
        if(state.completed){operation(state.completed->request.operation);slotIndex(state.completed->request.slot);
            if(state.completed->diagnostic.size()>1024||(!state.completed->diagnostic.empty()&&state.completed->success))throw savefile::Error("Invalid Cart completion state");}
        if(state.pending&&state.completed)throw savefile::Error("Conflicting Cart pending/completed state");
        state_=state;
    }
    const savefile::fs::path& directory() const{return directory_;}
};
}
