#pragma once
#include "state_bytes.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace edcamera {
struct Segment {std::array<uint16_t,4> selectors{};std::array<uint8_t,3> fps{};};
struct Track {unsigned backgroundStart=0,frameCount=0;std::vector<Segment> segments;unsigned declaredSegments=0;};
struct Frame {
    std::array<float,16> viewProjection{};
    std::array<float,2> groundRight{},groundForward{};
    std::array<float,3> eye{},right{},up{};
};
namespace detail {
struct Reader {
    const std::vector<uint8_t>& bytes;size_t cursor=0;
    void range(size_t offset,size_t count) const {
        if(offset>bytes.size()||count>bytes.size()-offset)throw std::runtime_error("Camera data is truncated");
    }
    uint8_t u8(){range(cursor,1);return bytes[cursor++];}
    uint16_t u16(){const uint16_t a=u8();return uint16_t(a|(uint16_t(u8())<<8));}
    uint32_t u32(){const uint32_t a=u16();return a|(uint32_t(u16())<<16);}
    float number(){const uint32_t bits=u32();float value;std::memcpy(&value,&bits,4);
        if(!std::isfinite(value))throw std::runtime_error("Camera data contains a non-finite value");return value;}
};
}
inline std::vector<Track> readSourceTracks(const std::vector<uint8_t>& native) {
    detail::Reader in{native};in.range(0,88);in.cursor=64;
    const unsigned count=in.u32();size_t offset=in.u32();in.cursor=76;const size_t end=in.u32();
    if(!count||count>256||offset<88||offset>end||end>native.size())throw std::runtime_error("Invalid source camera table");
    std::vector<Track> result;
    for(unsigned i=0;i<count;++i) {
        if(offset>end||end-offset<92)throw std::runtime_error("Truncated source camera header");
        Track track;in.cursor=offset;track.backgroundStart=in.u8();in.u8();const unsigned segments=in.u16();
        in.cursor=offset+84;track.frameCount=in.u32();
        if(!segments||segments>5||!track.frameCount||track.frameCount>(end-offset-92)/28)
            throw std::runtime_error("Invalid source camera segment/frame count");
        track.declaredSegments=segments;
        //417740 indexes the fixed five slots without consulting the declared count.
        for(unsigned s=0;s<5;++s) {
            Segment segment;in.cursor=offset+s*16+4;
            for(auto& value:segment.selectors)value=in.u16();
            in.cursor=offset+s*16+14;for(auto& value:segment.fps)value=in.u8();
            track.segments.push_back(segment);
        }
        offset+=92+size_t(track.frameCount)*28;result.push_back(std::move(track));
    }
    if(offset!=end)throw std::runtime_error("Unindexed source camera bytes");
    return result;
}
inline std::vector<std::vector<Frame>> readCameraFrames(const std::vector<uint8_t>& bytes,
                                                       const std::vector<Track>& expected={}) {
    detail::Reader in{bytes};const char magic[8]={'E','D','C','A','M','F','1',0};
    for(char c:magic)if(in.u8()!=uint8_t(c))throw std::runtime_error("Invalid camera frames magic");
    const unsigned count=in.u32();
    if(!count||count>256||(!expected.empty()&&expected.size()!=count))throw std::runtime_error("Invalid camera frame track count");
    std::vector<std::vector<Frame>> result;size_t total=0;
    for(unsigned track=0;track<count;++track) {
        const unsigned amount=in.u32();total+=amount;
        if(!amount||amount>10000||total>100000||(!expected.empty()&&expected[track].frameCount!=amount))
            throw std::runtime_error("Invalid camera frame count");
        in.range(in.cursor,size_t(amount)*116);std::vector<Frame> frames;
        for(unsigned frame=0;frame<amount;++frame) {
            Frame value;for(auto& v:value.viewProjection)v=in.number();
            for(auto& v:value.groundRight)v=in.number();for(auto& v:value.groundForward)v=in.number();
            for(auto& v:value.eye)v=in.number();for(auto& v:value.right)v=in.number();for(auto& v:value.up)v=in.number();
            const auto unit=[](const auto& axis){double sum=0;for(float v:axis)sum+=double(v)*v;return std::abs(sum-1)<.0002;};
            if(!unit(value.groundRight)||!unit(value.groundForward)||!unit(value.right)||!unit(value.up))
                throw std::runtime_error("Invalid camera frame basis");
            frames.push_back(value);
        }
        result.push_back(std::move(frames));
    }
    if(in.cursor!=bytes.size())throw std::runtime_error("Trailing camera frame bytes");
    return result;
}
struct Request {
    int mode=4,track=0,segment=0,startSelector=1,endSelector=2;
    bool operator==(const Request& other) const {
        return mode==other.mode&&track==other.track&&segment==other.segment&&
            startSelector==other.startSelector&&endSelector==other.endSelector;
    }
};
struct State {Request request;unsigned frame=0,start=0,end=0,fps=0,period=0;};
class Timeline {
    std::optional<Request> pending;
    size_t declaredCount(const Track& track) const {
        const size_t count=track.declaredSegments?track.declaredSegments:track.segments.size();
        if(!count||count>5||count>track.segments.size())throw std::runtime_error("Invalid declared camera segment count");
        return count;
    }
    uint64_t fingerprint(bool legacy=false) const {
        if(tracks.empty()||tracks.size()>256)throw std::runtime_error("Invalid camera asset track count");
        uint64_t hash=14695981039346656037ull;
        const auto word=[&](uint32_t value){for(unsigned b=0;b<4;++b){hash^=uint8_t(value>>(b*8));hash*=1099511628211ull;}};
        word(uint32_t(tracks.size()));
        for(const auto& track:tracks) {
            if(track.backgroundStart>255||!track.frameCount||track.frameCount>10000||track.segments.empty()||track.segments.size()>5)
                throw std::runtime_error("Invalid camera asset fingerprint input");
            const size_t declared=declaredCount(track),count=legacy?declared:track.segments.size();
            word(track.backgroundStart);word(track.frameCount);word(uint32_t(count));
            for(size_t s=0;s<count;++s){const auto& segment=track.segments[s];for(auto value:segment.selectors)word(value);for(auto value:segment.fps)word(value);}
        }
        return hash;
    }
    void validateFrame(const State& resolved,unsigned frame) const {
        const auto& track=tracks[size_t(resolved.request.track)];
        bool reachable=frame==resolved.frame;
        if(resolved.request.mode<2)reachable=reachable||frame==resolved.end||
            (resolved.frame<=frame&&frame<=resolved.end);
        else if(resolved.request.mode==4)reachable=reachable||
            (resolved.start<=frame&&frame<=resolved.end);
        if(frame>=track.frameCount||!reachable)throw std::runtime_error("Saved camera frame is outside its original timeline");
    }
    void validateCountdown(const std::optional<State>& value,int32_t ticks) const {
        // An inherited countdown can exceed the current period after control.
        // All authored byteFPS periods are0..30; fresh/no-camera state stays1.
        if(ticks<0||ticks>30||(!value&&ticks!=1))throw std::runtime_error("Invalid saved camera countdown");
    }
public:
    std::vector<Track> tracks;uint32_t scene=0;
    std::optional<State> state;
    int32_t countdown=1; //415D19 per-scene initialization; control does not reset it.
    bool publicationEnabled=true; //417580(1) clears the native suppression flag.
    Timeline(std::vector<Track> source,uint32_t identity):tracks(std::move(source)),scene(identity){}
    Timeline(const std::vector<uint8_t>& native,uint32_t identity):Timeline(readSourceTracks(native),identity){}
    bool inControl() const {return pending.has_value();}
    State resolve(Request request) const {
        if(request.mode<0||request.mode>4||request.track<0||size_t(request.track)>=tracks.size())
            throw std::runtime_error("Unsupported camera mode/track");
        const auto& track=tracks[size_t(request.track)];
        if(request.segment<0||request.segment>=5||size_t(request.segment)>=track.segments.size()||request.startSelector<0||
           request.startSelector>3||request.endSelector<0||request.endSelector>3)
            throw std::runtime_error("Unsupported camera segment/selector");
        const auto& segment=track.segments[size_t(request.segment)];State result;result.request=request;
        result.frame=segment.selectors[size_t(request.startSelector)];
        result.start=scene==0x61630400u?0:result.frame; //41792E forces AC04 loopStart0.
        result.end=segment.selectors[scene==0x61640400u?3:size_t(request.endSelector)];
        result.fps=segment.fps[request.startSelector<2?size_t(request.startSelector):2];
        result.period=result.fps?30/result.fps:2;
        if(result.frame>=track.frameCount||result.start>=track.frameCount||result.end>=track.frameCount)
            throw std::runtime_error("Authored camera endpoint is outside frame data");
        return result;
    }
    bool control(Request request,const std::function<void(int,int)>& callback={}) {
        if(pending){pending=request;return false;}
        pending=request;
        try{if(callback)callback(request.track,0);}catch(...){pending.reset();throw;}
        const Request chosen=*pending;pending.reset();
        if(state&&state->request==chosen)return false;
        const State candidate=resolve(chosen);
        if(!publicationEnabled)return false;
        state=candidate;return true;
    }
    bool tick(int engineMode=0,bool paused=false) {
        if(!state||engineMode!=0||paused)return false;
        if(--countdown>0)return false;
        countdown=int32_t(state->period);unsigned frame=state->frame+1;
        if(state->request.mode<2){if(frame>=state->end)frame=state->end;}
        else if(state->request.mode<4)frame=state->frame;
        else if(frame>state->end)frame=state->start;
        if(frame>=tracks[size_t(state->request.track)].frameCount)throw std::runtime_error("Camera worker selected an unavailable frame");
        state->frame=frame;return true; //41C530 also republishes an unchanged held frame.
    }
    unsigned backgroundIndex() const {
        if(!state)throw std::runtime_error("No camera has been published");
        return tracks[size_t(state->request.track)].backgroundStart+state->frame;
    }
    std::vector<uint8_t> snapshot() const {
        if(inControl())throw std::runtime_error("Cannot save during reentrant camera control");
        validateCountdown(state,countdown);
        if(state) {
            const auto derived=resolve(state->request);validateFrame(derived,state->frame);
            if(state->start!=derived.start||state->end!=derived.end||state->fps!=derived.fps||state->period!=derived.period)
                throw std::runtime_error("Camera state disagrees with original asset");
        }
        statebytes::Writer out;out.text("EDCAMERA2");out.put(scene);out.put(fingerprint());
        out.put(countdown);out.flag(publicationEnabled);out.flag(state.has_value());
        if(state) {
            const auto& request=state->request;
            for(int value:{request.mode,request.track,request.segment,request.startSelector,request.endSelector})out.put<int32_t>(value);
            out.put<uint32_t>(state->frame);
        }
        return std::move(out.bytes);
    }
    void restore(const std::vector<uint8_t>& bytes) {
        if(inControl())throw std::runtime_error("Cannot restore during reentrant camera control");
        if(bytes.size()>128)throw std::runtime_error("Excessive saved camera state");
        statebytes::Reader in{bytes};
        const auto magic=in.text();const bool legacy=magic=="EDCAMERA1";
        if((!legacy&&magic!="EDCAMERA2")||in.get<uint32_t>()!=scene||in.get<uint64_t>()!=fingerprint(legacy))
            throw std::runtime_error("Saved camera asset/scene does not match");
        const auto ticks=in.get<int32_t>();const bool enabled=in.flag();std::optional<State> candidate;
        if(in.flag()) {
            Request request;request.mode=in.get<int32_t>();request.track=in.get<int32_t>();request.segment=in.get<int32_t>();
            request.startSelector=in.get<int32_t>();request.endSelector=in.get<int32_t>();
            if(legacy&&(request.track<0||size_t(request.track)>=tracks.size()||request.segment<0||
                    size_t(request.segment)>=declaredCount(tracks[size_t(request.track)])))
                throw std::runtime_error("Legacy camera segment exceeds its original declared count");
            const auto frame=in.get<uint32_t>();candidate=resolve(request);validateFrame(*candidate,frame);candidate->frame=frame;
        }
        validateCountdown(candidate,ticks);in.end();
        state=std::move(candidate);countdown=ticks;publicationEnabled=enabled;
    }
};
}
