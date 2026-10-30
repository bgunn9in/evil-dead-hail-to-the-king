#include "source_camera_timeline.h"
#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
void integer(std::vector<uint8_t>& data,uint32_t value){for(unsigned b=0;b<4;++b)data.push_back(uint8_t(value>>(b*8)));}
void number(std::vector<uint8_t>& data,float value){uint32_t bits;std::memcpy(&bits,&value,4);integer(data,bits);}
void patch(std::vector<uint8_t>& data,size_t offset,uint32_t value){for(unsigned b=0;b<4;++b)data[offset+b]=uint8_t(value>>(b*8));}
std::vector<edcamera::Track> tracks(unsigned fps=15) {
    edcamera::Segment a{{{0,1,3,5}},{{uint8_t(fps),uint8_t(fps),uint8_t(fps)}}},b{{{3,4,6,7}},a.fps};
    return {{0,8,{a,b}},{10,8,{a,b}}};
}
std::vector<uint8_t> source() {
    std::vector<uint8_t> bytes(88+92+8*28);patch(bytes,64,1);patch(bytes,68,88);patch(bytes,76,unsigned(bytes.size()));
    bytes[88]=9;bytes[90]=2;patch(bytes,88+84,8);
    const auto fixture=tracks()[0];
    for(unsigned s=0;s<2;++s) {
        for(unsigned p=0;p<4;++p){const auto value=fixture.segments[s].selectors[p];bytes[88+s*16+4+p*2]=uint8_t(value);}
        for(unsigned p=0;p<3;++p)bytes[88+s*16+14+p]=fixture.segments[s].fps[p];
    }
    return bytes;
}
std::vector<uint8_t> frames() {
    std::vector<uint8_t> bytes={'E','D','C','A','M','F','1',0};integer(bytes,1);integer(bytes,2);
    for(unsigned frame=0;frame<2;++frame) {
        for(unsigned i=0;i<16;++i)number(bytes,i%5==0?1.f:0.f);
        for(float v:{1.f,0.f,0.f,1.f,1.f,2.f,float(3+frame),1.f,0.f,0.f,0.f,1.f,0.f})number(bytes,v);
    }
    return bytes;
}
template<class F>void rejects(F call){bool failed=false;try{call();}catch(const std::runtime_error&){failed=true;}assert(failed);}
std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);assert(input);return {(std::istreambuf_iterator<char>(input)),{}};
}
}
int main(int argc,char** argv) {
    using namespace edcamera;
    auto parsed=readSourceTracks(source());assert(parsed.size()==1&&parsed[0].frameCount==8&&parsed[0].backgroundStart==9);
    assert(parsed[0].segments.size()==5&&parsed[0].declaredSegments==2&&parsed[0].segments[1].selectors[1]==4&&parsed[0].segments[0].fps[1]==15);
    Timeline reserved(parsed,0);reserved.control({3,0,2,1,2});assert(reserved.state->frame==0&&reserved.state->period==2);
    assert(reserved.resolve({3,0,4,1,2}).frame==0);
    rejects([&]{reserved.resolve({3,0,-1,1,2});});rejects([&]{reserved.resolve({3,0,5,1,2});});
    auto badEndpoint=parsed;badEndpoint[0].segments[2].selectors[1]=8;
    rejects([&]{Timeline(badEndpoint,0).resolve({3,0,2,1,2});});
    auto data=source();patch(data,76,unsigned(data.size()-1));rejects([&]{readSourceTracks(data);});
    data=source();data[90]=6;rejects([&]{readSourceTracks(data);});
    const auto asset=frames();auto decoded=readCameraFrames(asset);assert(decoded[0][1].eye[2]==4&&decoded[0][0].up[1]==1);
    for(size_t size=0;size<asset.size();++size)rejects([&]{readCameraFrames({asset.begin(),asset.begin()+size});});
    data=asset;data.push_back(0);rejects([&]{readCameraFrames(data);});
    data=asset;patch(data,80,0x7fc00000);rejects([&]{readCameraFrames(data);});
    data=asset;patch(data,80,0);rejects([&]{readCameraFrames(data);});
    rejects([&]{readCameraFrames(asset,parsed);});parsed[0].frameCount=2;assert(readCameraFrames(asset,parsed)[0].size()==2);
    Timeline loop(tracks(),0);assert(loop.control({4,0,0,1,2}));std::vector<unsigned> actual{loop.state->frame};
    for(unsigned tick=0;tick<7;++tick){loop.tick();actual.push_back(loop.state->frame);}
    assert(actual==std::vector<unsigned>({1,2,2,3,3,1,1,2}));
    for(int mode:{0,1,2,3}) {
        Timeline timer(tracks(30),0);timer.control({mode,0});
        for(unsigned tick=0;tick<10;++tick)assert(timer.tick());
        assert(timer.state->frame==(mode<2?3u:1u));
    }
    for(const auto [fps,period]:std::vector<std::pair<unsigned,unsigned>>{{0,2},{7,4},{15,2},{30,1},{255,0}})
        assert(Timeline(tracks(fps),0).resolve({4,0}).period==period);
    Timeline timer(tracks(7),0);timer.control({4,0});assert(!timer.tick(2)&&!timer.tick(0,true)&&timer.countdown==1);
    timer.tick();timer.tick();assert(timer.countdown==3);unsigned callbacks=0;
    assert(!timer.control({4,0},[&](int,int){++callbacks;}));assert(callbacks==1&&timer.state->frame==2&&timer.countdown==3);
    assert(timer.control({4,1},[&](int track,int frame){
        assert(track==1&&frame==0&&timer.state->request.track==0);++callbacks;
        timer.control({3,0});timer.control({4,1,1});
    }));
    assert(callbacks==2&&timer.state->request.segment==1&&timer.state->frame==4&&timer.countdown==3);
    timer.publicationEnabled=false;assert(!timer.control({3,0})&&timer.state->request.track==1);
    assert(Timeline(tracks(),0x61630400).resolve({4,0,0,3,2}).start==0);
    assert(Timeline(tracks(),0x61640400).resolve({4,0}).end==5);
    rejects([&]{timer.resolve({5,0});});rejects([&]{timer.resolve({4,2});});rejects([&]{timer.resolve({4,0,2});});
    const auto saved=timer.snapshot();assert(saved.size()==55&&!timer.inControl());
    Timeline resumed(tracks(7),0);resumed.restore(saved);
    assert(resumed.snapshot()==saved&&!resumed.publicationEnabled&&resumed.state->frame==4&&resumed.countdown==3);
    for(unsigned tick=0;tick<20;++tick) {
        assert(timer.tick()==resumed.tick());assert(timer.state->frame==resumed.state->frame&&timer.countdown==resumed.countdown);
    }
    const auto stable=resumed.snapshot();
    for(size_t size=0;size<stable.size();++size) {
        rejects([&]{resumed.restore({stable.begin(),stable.begin()+size});});assert(resumed.snapshot()==stable);
    }
    for(const auto [offset,value]:std::vector<std::pair<size_t,uint32_t>>{{13,1},{17,1},{25,31},{25,0xffffffff},
            {31,5},{35,2},{39,2},{43,4},{47,4},{51,3},{51,8}}) {
        data=stable;patch(data,offset,value);rejects([&]{resumed.restore(data);});assert(resumed.snapshot()==stable);
    }
    for(size_t offset:{29u,30u}){data=stable;data[offset]=2;rejects([&]{resumed.restore(data);});assert(resumed.snapshot()==stable);}
    data=stable;data.push_back(0);rejects([&]{resumed.restore(data);});assert(resumed.snapshot()==stable);
    Timeline different(tracks(15),0);rejects([&]{different.restore(saved);});
    different=Timeline(tracks(7),1);rejects([&]{different.restore(saved);});
    different=Timeline(tracks(7),0);different.tracks[1].segments[0].selectors[0]=1;
    rejects([&]{different.restore(saved);});
    Timeline empty(tracks(),0),cleared(tracks(),0);cleared.control({4,0});cleared.restore(empty.snapshot());
    assert(!cleared.state&&cleared.countdown==1);
    const auto before=resumed.snapshot();
    resumed.control({3,0},[&](int,int){
        assert(resumed.inControl());rejects([&]{resumed.snapshot();});rejects([&]{resumed.restore(saved);});
    });assert(!resumed.inControl());
    resumed.restore(before);resumed.state->period=99;rejects([&]{resumed.snapshot();});resumed.restore(before);
    statebytes::Writer old;old.text("EDCAMERA1");old.put<uint32_t>(0);old.put<uint64_t>(0xd0904d2f81194576ull);
    old.put<int32_t>(1);old.flag(true);old.flag(true);
    for(int value:{3,0,1,1,2})old.put<int32_t>(value);old.put<uint32_t>(4);
    Timeline upgraded(readSourceTracks(source()),0);upgraded.restore(old.bytes);
    assert(upgraded.state->frame==4&&upgraded.state->request.segment==1);
    const auto current=upgraded.snapshot();statebytes::Reader currentReader{current};assert(currentReader.text()=="EDCAMERA2");
    data=old.bytes;patch(data,39,2);rejects([&]{upgraded.restore(data);});assert(upgraded.snapshot()==current);
    data=old.bytes;patch(data,17,0);rejects([&]{upgraded.restore(data);});assert(upgraded.snapshot()==current);
    auto altered=readSourceTracks(source());altered[0].segments[3].selectors[0]=1;
    Timeline changedReserved(altered,0);rejects([&]{changedReserved.restore(current);});
    changedReserved.restore(old.bytes);assert(changedReserved.state->frame==4);
    upgraded.restore(current);assert(upgraded.snapshot()==current);
    if(argc>1) {
        const std::filesystem::path root=argv[1];unsigned total=0;
        std::ifstream manifest(root/"world.txt");std::string magic,first;unsigned count=0;manifest>>magic>>first>>count;
        assert(manifest&&magic=="EDWORLD1"&&count>0&&count<=256);
        for(unsigned room=0;room<count;++room) {
            std::string scene;manifest>>scene;assert(manifest&&scene.size()==4);
            const auto original=readSourceTracks(read(root/scene/"scene.native"));
            const auto rendered=readCameraFrames(read(root/scene/"camera-frames.bin"),original);
            for(size_t i=0;i<original.size();++i){total+=original[i].frameCount;assert(rendered[i].size()==original[i].frameCount);}
            if(std::string(scene)=="ak01") {
                Timeline native(original,0x616b0100);native.control({4,0});assert(native.state->frame==0);
                for(unsigned tick=1;tick<=31;++tick){native.tick();assert(native.state->frame==((tick+1)/2)%15);}
            }
            if(std::string(scene)=="ak09") {
                Timeline native(original,0x616b0900);native.control({4,8});
                assert(native.state->frame==15&&native.state->end==28&&native.state->period==2&&native.backgroundIndex()==62);
                const auto originalSave=native.snapshot();Timeline continued(original,0x616b0900);continued.restore(originalSave);
                assert(continued.state->frame==15&&continued.countdown==1&&continued.backgroundIndex()==62);
                for(unsigned tick=0;tick<40;++tick){assert(native.tick()==continued.tick());assert(native.snapshot()==continued.snapshot());}
            }
        }
        assert(total>500);
        const auto bm01=std::filesystem::is_regular_file(root/"levelB"/"bm01"/"scene.native")?
            root/"levelB"/"bm01"/"scene.native":root/"bm01"/"scene.native";
        if(std::filesystem::is_regular_file(bm01)) {
            const auto authored=readSourceTracks(read(bm01));
            assert(authored[0].declaredSegments==1&&authored[0].frameCount==1&&authored[0].segments.size()==5);
            Timeline original(authored,0x626d0100);original.control({3,0,1,1,2});
            assert(original.state->frame==0&&original.state->period==2);
            const auto checkpoint=original.snapshot();Timeline replay(authored,0x626d0100);replay.restore(checkpoint);
            for(unsigned tick=0;tick<8;++tick){original.tick();replay.tick();assert(original.snapshot()==replay.snapshot());}
            statebytes::Writer previous;previous.text("EDCAMERA1");previous.put<uint32_t>(0x626d0100);
            previous.put<uint64_t>(0xe5fee5978cfcb045ull);previous.put<int32_t>(1);previous.flag(true);previous.flag(true);
            for(int value:{3,1,1,1,2})previous.put<int32_t>(value);previous.put<uint32_t>(1);
            replay.restore(previous.bytes);assert(replay.state->request.track==1&&replay.state->frame==1);
            data=previous.bytes;patch(data,35,0);rejects([&]{replay.restore(data);});
        }
    }
}
