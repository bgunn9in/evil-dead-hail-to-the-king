#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "input.h"
#include "viewport.h"
#include "ground.h"
#include "triggers.h"
#include "doors.h"
#include "script_scene.h"
#include "source_scene_queries.h"
#include "source_gui.h"
#include "source_actor_math.h"
#include "source_actor_routes.h"
#include "source_floor_query.h"
#include "jpc.h"
#include "script_game.h"
#include "episode_portals.h"
#include "audio_bank.h"
#include "audio_spatial.h"
#include "stream_audio.h"
#include "source_effects.h"
#include "source_camera_timeline.h"
#include "source_animation_event.h"
#include "source_animation_sound.h"
#include "native_frame_clock.h"
#include "package_arrival.h"
#include "cart_adapter.h"
#include "source_firearm.h"
#include "save_file.h"
#include "state_bytes.h"
#include "pause_menu.h"
#include "debug_menu.h"
#include "control_menu.h"
#include "control_settings.h"
#include "native_locomotion.h"
#include "native_input_queue.h"
#include "source_rumble.h"
#include "source_rumble_sdl.h"
#include "video_settings.h"
#include "settings_menu.h"
#include "utility_menu_presentation.h"
#include "movie_player.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
struct Vertex { float x, y, z, nx, ny, nz, u, v; };
struct Camera {
    std::array<float,16> vp{};
    Vec2 right,forward;
    std::array<float,3> eye3{},right3{},up3{};
};
struct Scene {
    std::string id;
    unsigned vertexCount = 0;
    float spawn[3]{}, scale = 1, speed = 1;
    unsigned idleFrames = 0, walkFrames = 0;
    float animationFPS = 30;
    std::vector<Vertex> vertices;
    std::vector<Uint8> idle, walk;
    std::vector<Camera> cameras;
    std::vector<edcamera::Track> cameraTracks;
    std::vector<std::vector<edcamera::Frame>> cameraFrames;
    std::vector<unsigned> cameraFrame,foregroundFrame;
    float radius = 0;
    std::vector<GroundPolygon> ground;
    std::vector<FloorTriangle> floor;
    TriggerGrid triggerGrid;
    std::vector<CameraTrigger> triggers;
    std::vector<Door> doors;
    ScriptScene scripting;
    std::vector<Uint8> nativeBytes;
    SourceSceneQueries queries;
    SourceFloorQuery sourceFloor;
    bool episode=false,menuContext=false;
    EpisodePortals portalData;
    std::vector<CameraTrigger> nativePortals;
    std::vector<bool> enabledVolumes,enabledPortals;
    std::vector<unsigned> cameraSources;
    std::vector<uint32_t> cameraMasks;
};
void setCameraFrame(Scene& scene,unsigned track,unsigned frame) {
    if(track>=scene.cameras.size())throw std::runtime_error("Camera track is unavailable");
    if(scene.cameraFrames.empty()){if(frame)throw std::runtime_error("Animated camera frames have not been exported");return;}
    const auto& source=scene.cameraFrames.at(track).at(frame);auto& target=scene.cameras[track];
    target.vp=source.viewProjection;target.right={source.groundRight[0],source.groundRight[1]};
    target.forward={source.groundForward[0],source.groundForward[1]};target.eye3=source.eye;target.right3=source.right;target.up3=source.up;
    scene.cameraFrame[track]=frame;
}

void setVolumeEnabled(Scene& scene,unsigned index,bool enabled) {
    if(index>=scene.enabledVolumes.size())throw std::runtime_error("Native activation volume is missing");
    scene.enabledVolumes[index]=enabled;
    for(auto& polygon:scene.ground)if(polygon.sourceIndex==index)polygon.enabled=enabled;
    for(size_t i=0;i<scene.cameraSources.size();++i)if(scene.cameraSources[i]==index)
        scene.triggers[i].mask=enabled?scene.cameraMasks[i]:0;
}

template<class T> T* checked(T* pointer, const char* context) {
    if (!pointer) throw std::runtime_error(std::string(context) + ": " + SDL_GetError());
    return pointer;
}
void checked(bool ok, const char* context) {
    if (!ok) throw std::runtime_error(std::string(context) + ": " + SDL_GetError());
}
std::vector<Uint8> readFile(const fs::path& path, size_t limit = 64 * 1024 * 1024) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<uint64_t>(input.tellg()) > limit)
        throw std::runtime_error("Cannot read file or file too large: " + path.string());
    std::vector<Uint8> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Truncated file: " + path.string());
    return bytes;
}
template<class T> T take(const std::vector<Uint8>& data, size_t& offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset) throw std::runtime_error("Truncated scene rules");
    T value; std::memcpy(&value, data.data() + offset, sizeof(value)); offset += sizeof(value);
    return value;
}
float takeFloat(const std::vector<Uint8>& data, size_t& offset) {
    const float value = take<float>(data, offset);
    if (!std::isfinite(value)) throw std::runtime_error("Non-finite scene rule coordinate");
    return value;
}
bool validSceneId(const std::string& id) {
    return id.size()==4&&std::all_of(id.begin(),id.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');});
}
std::string takeSceneId(const std::vector<Uint8>& data,size_t& offset) {
    if (offset>data.size()||data.size()-offset<4) throw std::runtime_error("Truncated scene identity");
    const std::string id(reinterpret_cast<const char*>(data.data()+offset),4); offset+=4;
    if (!validSceneId(id)) throw std::runtime_error("Invalid scene identity");
    return id;
}
CameraTrigger takeRegion(const std::vector<Uint8>& data,size_t& offset,bool allowEmpty) {
    CameraTrigger region;
    region.shape=take<uint32_t>(data,offset);
    const unsigned count=take<uint32_t>(data,offset);
    region.mask=take<uint32_t>(data,offset);
    if ((region.shape!=0&&region.shape!=2)||(!allowEmpty||count)&&(count!=(region.shape==0?2u:4u)))
        throw std::runtime_error("Invalid door region");
    for (unsigned i=0;i<count;++i) region.points.push_back({takeFloat(data,offset),takeFloat(data,offset)});
    if (region.shape==0&&count&&(region.points[0].x>region.points[1].x||region.points[0].z>region.points[1].z))
        throw std::runtime_error("Invalid rectangular door region");
    if (count&&region.shape==0&&(region.points[0].x==region.points[1].x||region.points[0].z==region.points[1].z))
        throw std::runtime_error("Degenerate door rectangle");
    if (region.shape==2&&count) for (unsigned i=0;i<count;++i) {
        const Vec2 a=region.points[i],b=region.points[(i+1)%count],c=region.points[(i+2)%count];
        if ((double(b.x)-a.x)*(double(c.z)-b.z)-(double(b.z)-a.z)*(double(c.x)-b.x)>=0)
            throw std::runtime_error("Door quadrilateral must be convex and clockwise");
    }
    return region;
}
void loadDoors(Scene& scene,const fs::path& root) {
    const auto data=readFile(root/"doors.bin");
    if (data.size()<16||std::memcmp(data.data(),"EDDOOR1\0",8)) throw std::runtime_error("Invalid door header");
    size_t offset=8; scene.id=takeSceneId(data,offset);
    const unsigned count=take<uint32_t>(data,offset);
    if (count>256) throw std::runtime_error("Too many source portals");
    for (unsigned i=0;i<count;++i) {
        Door door; door.portalIndex=take<uint32_t>(data,offset);
        door.interaction=takeRegion(data,offset,true);
        door.marker={takeFloat(data,offset),takeFloat(data,offset),takeFloat(data,offset)};
        door.facingArg=takeFloat(data,offset);
        door.soundId=take<uint32_t>(data,offset); door.delayTicks=take<uint32_t>(data,offset);
        door.maxTicks=take<uint32_t>(data,offset); door.guardTicks=take<uint32_t>(data,offset);
        if (!door.interaction.points.empty()&&(!door.delayTicks||door.maxTicks<door.delayTicks||door.maxTicks>10000||
            door.guardTicks>10000||door.facingArg<=0||door.facingArg>360)) throw std::runtime_error("Invalid source door policy");
        door.portal=takeRegion(data,offset,false); door.destination=takeSceneId(data,offset);
        door.destinationCamera=take<uint32_t>(data,offset);
        door.spawn={takeFloat(data,offset),takeFloat(data,offset),takeFloat(data,offset)};
        door.yaw=takeFloat(data,offset);
        for (const auto& other:scene.doors) if (other.portalIndex==door.portalIndex) throw std::runtime_error("Duplicate source portal identity");
        scene.doors.push_back(std::move(door));
    }
    if (offset!=data.size()) throw std::runtime_error("Unindexed source door bytes");
}
void loadFloor(Scene& scene,const fs::path& directory) {
    const auto data=readFile(directory/"floor.bin");
    if (data.size()<12 || std::memcmp(data.data(),"EDFLOOR1",8)) throw std::runtime_error("Invalid floor header");
    size_t offset=8;
    const unsigned count=take<uint32_t>(data,offset);
    if (!count || count>100000 || data.size()!=12+size_t(count)*36) throw std::runtime_error("Invalid floor triangle count");
    for (unsigned i=0;i<count;++i) {
        GroundPoint points[3];
        for (auto& point:points) point={takeFloat(data,offset),takeFloat(data,offset),takeFloat(data,offset)};
        scene.floor.push_back({points[0],points[1],points[2]});
    }
    if(scene.menuContext)return;
    const auto height=floorHeight(scene.floor,{scene.spawn[0],scene.spawn[2]});
    if (!height) throw std::runtime_error("Scene spawn has no original floor triangle");
    scene.spawn[1]=*height;
}
void loadRules(Scene& scene, const fs::path& directory) {
    auto data = readFile(directory / "collision.bin");
    if (data.size() < 16 || std::memcmp(data.data(), "EDCOLL1\0", 8)) throw std::runtime_error("Invalid collision header");
    size_t offset = 8;
    const unsigned count = take<uint32_t>(data, offset);
    scene.radius = takeFloat(data, offset);
    if (!count || count > 10000 || scene.radius <= 0 || scene.radius > 2) throw std::runtime_error("Invalid collision count/radius");
    for (unsigned i = 0; i < count; ++i) {
        const unsigned mode = take<uint32_t>(data, offset), points = take<uint32_t>(data, offset);
        if (mode > 3 || (mode>=2?points!=2:points<3) || points > 64) throw std::runtime_error("Invalid collision polygon");
        GroundPolygon polygon; polygon.keepInside = mode == 1; polygon.boundaryOnly=mode==2;
        for (unsigned j = 0; j < points; ++j) {
            const float x = takeFloat(data, offset), z = takeFloat(data, offset);
            polygon.points.push_back({x,z});
        }
        if(mode==3) {
            polygon.circleRadius=polygon.points[1].x;polygon.points.resize(1);
            if(polygon.circleRadius<=0||polygon.circleRadius>10000)throw std::runtime_error("Invalid source circle radius");
            scene.ground.push_back(std::move(polygon));continue;
        }
        float area = 0;
        for (unsigned j = 0; j < (mode==2?points-1:points); ++j) {
            const Vec2 a = polygon.points[j], b = polygon.points[(j+1)%points];
            if (length(b-a) < 1e-6f) throw std::runtime_error("Degenerate collision edge");
            area += a.x*b.z-b.x*a.z;
        }
        if (mode!=2&&std::abs(area) < 1e-6f) throw std::runtime_error("Degenerate collision polygon");
        scene.ground.push_back(std::move(polygon));
    }
    if (offset != data.size()) throw std::runtime_error("Unindexed collision bytes");
    data = readFile(directory / "triggers.bin");
    auto parsed=readCameraTriggers(data,unsigned(scene.cameras.size()));
    scene.triggerGrid=parsed.grid;scene.triggers=std::move(parsed.triggers);
    if (!scene.menuContext&&!clearGround(scene.ground,{scene.spawn[0],scene.spawn[2]},0))
        throw std::runtime_error("Scene spawn overlaps original collision geometry");
}
void loadNativeScene(Scene& scene,const fs::path& directory,unsigned cameraCount) {
    const auto native=readFile(directory/"scene.native");
    scene.nativeBytes=native;
    scene.scripting=ScriptScene(native);
    scene.queries=SourceSceneQueries(native);
    scene.sourceFloor=SourceFloorQuery(native);
    auto nativeInteger=[&](size_t offset) {
        if(offset>native.size()||native.size()-offset<4)throw std::runtime_error("Native scene reference is truncated");
        uint32_t value;std::memcpy(&value,native.data()+offset,4);return value;
    };
    if(nativeInteger(0x40)!=cameraCount)throw std::runtime_error("Native camera count mismatch");
    size_t cameraOffset=nativeInteger(0x44);
    for(auto& camera:scene.cameras) {
        const unsigned frames=nativeInteger(cameraOffset+0x54);
        const size_t end=cameraOffset+92+uint64_t(frames)*28;
        if(!frames||end>native.size())throw std::runtime_error("Truncated native camera frames");
        for(unsigned i=0;i<3;++i) {
            std::memcpy(&camera.eye3[i],native.data()+cameraOffset+92+i*4,4);
            if(!std::isfinite(camera.eye3[i]))throw std::runtime_error("Invalid native camera eye");
            camera.right3[i]=camera.vp[i*4];camera.up3[i]=camera.vp[i*4+1];
        }
        for(auto* axis:{&camera.right3,&camera.up3}) {
            double square=0;for(float value:*axis)square+=double(value)*value;
            if(square<1e-12)throw std::runtime_error("Invalid native camera axis");
            for(float& value:*axis)value=float(value/std::sqrt(square));
        }
        cameraOffset=end;
    }
    scene.cameraTracks=edcamera::readSourceTracks(native);scene.cameraFrame.assign(cameraCount,0);scene.foregroundFrame.assign(cameraCount,0);
    if(fs::exists(directory/"camera-frames.bin")) {
        scene.cameraFrames=edcamera::readCameraFrames(readFile(directory/"camera-frames.bin",16*1024*1024),scene.cameraTracks);
        for(unsigned track=0;track<cameraCount;++track) {
            if(scene.cameraFrames[track][0].viewProjection!=scene.cameras[track].vp)throw std::runtime_error("Camera frame0 disagrees with scene projection");
        }
    }
    scene.enabledVolumes.assign(scene.scripting.volumes.size(),true);
    const unsigned portalCount=nativeInteger(0x28),portalOffset=nativeInteger(0x2c);
    if(portalCount>1000||portalOffset>native.size()||size_t(portalCount)*72>native.size()-portalOffset)
        throw std::runtime_error("Native portal table is truncated");
    scene.enabledPortals.assign(portalCount,true);
    for(unsigned i=0;i<portalCount;++i) {
        const size_t offset=portalOffset+size_t(i)*72;CameraTrigger portal;portal.shape=native[offset+1];portal.mask=nativeInteger(offset+52);
        if(portal.shape>2)throw std::runtime_error("Unsupported native portal shape");
        for(unsigned p=0;p<(portal.shape==0?2u:portal.shape==1?3u:4u);++p) {
            float x,z;std::memcpy(&x,native.data()+offset+4+p*12,4);std::memcpy(&z,native.data()+offset+12+p*12,4);
            if(!std::isfinite(x)||!std::isfinite(z))throw std::runtime_error("Non-finite native portal coordinate");portal.points.push_back({x,z});
        }
        scene.nativePortals.push_back(std::move(portal));
    }
    size_t collider=0;
    for(unsigned i=0;i<scene.scripting.volumes.size();++i)if(scene.scripting.volumes[i].kind==2&&scene.scripting.volumes[i].shape<=3) {
        if(collider>=scene.ground.size())throw std::runtime_error("Native static collision map is incomplete");
        scene.ground[collider++].sourceIndex=i;
    }
    const size_t volumeOffset=nativeInteger(12),ruleCount=nativeInteger(16),ruleOffset=nativeInteger(20),
                 actionCount=nativeInteger(24),actionOffset=nativeInteger(28);
    for(unsigned index=0;index<scene.scripting.volumes.size();++index)if(scene.scripting.volumes[index].kind==0) {
        const size_t offset=volumeOffset+size_t(index)*72,amount=nativeInteger(offset+52),first=nativeInteger(offset+56);
        if(first>ruleCount||amount>ruleCount-first)throw std::runtime_error("Native camera rule reference is invalid");
        bool camera=false;
        for(size_t rule=first;rule<first+amount;++rule) {
            const size_t actions=nativeInteger(ruleOffset+rule*16),start=nativeInteger(ruleOffset+rule*16+4);
            if(start>actionCount||actions>actionCount-start)throw std::runtime_error("Native camera action reference is invalid");
            for(size_t action=start;action<start+actions;++action)if((nativeInteger(actionOffset+action*8)&0xffffu)==0)camera=true;
        }
        if(!camera)continue;
        const size_t position=scene.cameraSources.size();
        if(position>=scene.triggers.size())throw std::runtime_error("Native camera volume map is incomplete");
        const auto& raw=scene.scripting.volumes[index];const auto& trigger=scene.triggers[position];
        if(raw.shape!=trigger.shape||raw.points.size()!=trigger.points.size()||nativeInteger(offset+60)!=trigger.mask)
            throw std::runtime_error("Native camera volume does not match exported trigger");
        for(size_t p=0;p<raw.points.size();++p)if(raw.points[p].x!=trigger.points[p].x||raw.points[p].z!=trigger.points[p].z)
            throw std::runtime_error("Native camera geometry does not match exported trigger");
        scene.cameraSources.push_back(index);scene.cameraMasks.push_back(trigger.mask);
    }
    if(scene.cameraSources.size()!=scene.triggers.size())throw std::runtime_error("Native camera volume count does not match");
    if(fs::is_regular_file(directory/"portals.bin")) {
        scene.episode=true;scene.portalData=readEpisodePortals(readFile(directory/"portals.bin"));
        if(scene.portalData.scene!=scene.id||scene.portalData.gridOrigin[0]!=scene.triggerGrid.minX||
           scene.portalData.gridOrigin[1]!=scene.triggerGrid.minZ||scene.portalData.gridExtent[0]!=scene.triggerGrid.extentX||
           scene.portalData.gridExtent[1]!=scene.triggerGrid.extentZ)throw std::runtime_error("Episode portal scene/grid mismatch");
        std::map<unsigned,Door> hints;for(auto& hint:scene.doors)hints.emplace(hint.portalIndex,std::move(hint));
        scene.doors.clear();
        for(const auto& portal:scene.portalData.portals) {
            if(portal.index>=portalCount)throw std::runtime_error("Episode portal is missing from native scene");
            const size_t offset=portalOffset+size_t(portal.index)*72;
            if(native[offset]!=0||native[offset+1]!=portal.shape||native[offset+2]!=portal.camera||
               native[offset+3]!=portal.marker||nativeInteger(offset+52)!=portal.mask||nativeInteger(offset+56)!=portal.destinationId)
                throw std::runtime_error("Episode portal metadata disagrees with native scene");
            Door door;const auto hint=hints.find(portal.index);if(hint!=hints.end()){door=std::move(hint->second);hints.erase(hint);}
            door.portalIndex=portal.index;door.portal.shape=portal.shape;door.portal.mask=portal.mask;door.portal.points.clear();
            for(size_t i=0;i<portal.points.size();++i) {
                float x,z;std::memcpy(&x,native.data()+offset+4+i*12,4);std::memcpy(&z,native.data()+offset+12+i*12,4);
                if(x!=portal.points[i][0]||z!=portal.points[i][1])throw std::runtime_error("Episode portal geometry disagrees with native scene");
                door.portal.points.push_back({x,z});
            }
            for(unsigned axis=0;axis<3;++axis){float value;std::memcpy(&value,native.data()+offset+60+axis*4,4);
                if(value!=portal.rotationXYZ[axis])throw std::runtime_error("Episode portal rotation disagrees with native scene");}
            door.destination=portal.destination;door.destinationCamera=portal.camera;door.yaw=portal.rotationXYZ[1];
            if(portal.resolved)door.spawn={portal.markerXYZ[0],0,portal.markerXYZ[2]};
            scene.doors.push_back(std::move(door));
        }
        size_t nativeTransitions=0;for(unsigned i=0;i<portalCount;++i)nativeTransitions+=native[portalOffset+i*72]==0;
        if(nativeTransitions!=scene.doors.size()||!hints.empty())throw std::runtime_error("Episode manifest omits native transitions");
    }
}
#include "menu_scene.inl"
Scene loadScene(const fs::path& directory) {
    if(fs::is_regular_file(directory/"menu.txt"))return loadMenuScene(directory);
    Scene scene;
    unsigned cameraCount = 0;
    std::string magic;
    std::ifstream input(directory / "scene.txt");
    input >> magic >> scene.vertexCount >> cameraCount;
    if (!input || (magic != "EDSCENE1" && magic != "EDSCENE2") || !scene.vertexCount || scene.vertexCount > 1000000 ||
        scene.vertexCount % 3 || !cameraCount || cameraCount > 256)
        throw std::runtime_error("Invalid scene header");
    input >> scene.spawn[0] >> scene.spawn[1] >> scene.spawn[2] >> scene.scale >> scene.speed;
    if (magic == "EDSCENE2") {
        input >> scene.idleFrames >> scene.walkFrames >> scene.animationFPS;
        if (!input || scene.idleFrames > 240 || scene.walkFrames > 240 ||
            !std::isfinite(scene.animationFPS) || scene.animationFPS <= 0 || scene.animationFPS > 240)
            throw std::runtime_error("Invalid animation parameters");
    }
    if (!input || !std::isfinite(scene.scale) || scene.scale <= 0 || !std::isfinite(scene.speed) || scene.speed <= 0)
        throw std::runtime_error("Invalid scene scale/speed");
    for (float value : scene.spawn) if (!std::isfinite(value)) throw std::runtime_error("Invalid spawn");
    scene.cameras.resize(cameraCount);
    for (Camera& camera : scene.cameras) {
        for (float& value : camera.vp) {
            input >> value;
            if (!std::isfinite(value)) throw std::runtime_error("Invalid camera matrix");
        }
        float rightY = 0, forwardY = 0;
        input >> camera.right.x >> rightY >> camera.right.z >> camera.forward.x >> forwardY >> camera.forward.z;
        if (!input || !std::isfinite(length(camera.right)) || !std::isfinite(length(camera.forward)) ||
            length(camera.right) < 0.1f || length(camera.forward) < 0.1f)
            throw std::runtime_error("Invalid camera basis");
    }
    const auto data = readFile(directory / "ash.bin");
    if (data.size() != scene.vertexCount * sizeof(Vertex)) throw std::runtime_error("Invalid mesh length");
    scene.vertices.resize(scene.vertexCount);
    std::memcpy(scene.vertices.data(), data.data(), data.size());
    for (const auto& vertex : scene.vertices) {
        const float values[] = {vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v};
        for (float value : values) if (!std::isfinite(value)) throw std::runtime_error("Invalid mesh coordinate");
    }
    auto loadClip = [&](const char* name, unsigned frames, std::vector<Uint8>& clip) {
        if (!frames) return;
        clip = readFile(directory / name, 256 * 1024 * 1024);
        if (clip.size() != uint64_t(frames) * scene.vertexCount * sizeof(Vertex))
            throw std::runtime_error(std::string("Invalid animation length: ") + name);
        for (size_t offset = 0; offset < clip.size(); offset += sizeof(float)) {
            float value; std::memcpy(&value, clip.data() + offset, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("Invalid animation coordinate");
        }
    };
    loadClip("idle.bin", scene.idleFrames, scene.idle);
    loadClip("walk.bin", scene.walkFrames, scene.walk);
    loadFloor(scene,directory);
    loadRules(scene,directory);
    loadDoors(scene,directory);
    loadNativeScene(scene,directory,cameraCount);
    return scene;
}
struct Actor {
    float position[3]{}, yaw = 0;
    float floorOffset=0;
    float clearanceRadius=-1;
    unsigned camera = 0;
    unsigned floorTriangle=std::numeric_limits<unsigned>::max();
    Movement movement;
    std::vector<bool> membership;
    double animationTime = 0;
    bool walking = false;
};
float initialActorRadius(const Scene& scene,Vec2 point) {
    if(!clearGround(scene.ground,point,0))throw std::runtime_error("Original actor point is inside a blocking volume");
    if(clearGround(scene.ground,point,scene.radius))return scene.radius;
    float clearance=scene.radius;
    for(const auto& polygon:scene.ground)if(polygon.enabled)clearance=std::min(clearance,nearestBoundary(polygon,point).distance);
    // Source marker4 is .038m from a wall. Let the modern capsule grow as the
    // player leaves that overlap instead of moving the original spawn point.
    return std::max(0.0f,clearance-ContactSkin);
}
Actor resetActor(const Scene& scene, unsigned initialCamera) {
    Actor actor;
    if(scene.menuContext){actor.camera=initialCamera;actor.membership.assign(scene.triggers.size(),false);actor.clearanceRadius=0;return actor;}
    std::memcpy(actor.position,scene.spawn,sizeof(actor.position)); actor.camera=initialCamera;
    actor.clearanceRadius=initialActorRadius(scene,{scene.spawn[0],scene.spawn[2]});
    actor.membership.assign(scene.triggers.size(),false);
    actor.camera=enterTriggers(scene.triggers,{actor.position[0],actor.position[2]},actor.membership,actor.camera,scene.triggerGrid);
    return actor;
}
struct ActorStep { float requested = 0, moved = 0; Vec2 desiredDelta{}; };
ActorStep stepActor(const Scene& scene, Actor& actor, Vec2 stick, bool run, float dt,bool sourcePose=false,const std::vector<GroundPolygon>& dynamic={}) {
    if(scene.menuContext)return {};
    const Camera& camera=scene.cameras[actor.camera];
    const Vec2 velocity=actor.movement.world(stick,camera.right,camera.forward);
    const Vec2 old{actor.position[0],actor.position[2]};
    // Native42B3D0 tests static geometry at the actor point. Nominal modern
    // separation belongs only to other actors, not the authored wall gaps.
    if(sourcePose)actor.clearanceRadius=0;
    if(sourcePose&&(length(velocity)<=0.001f||dt==0)) {
        // Native zero-XZ input preserves authored overlap; zero-time intent only turns.
        if(length(velocity)>0.001f)actor.yaw=std::atan2(velocity.x,velocity.z);
        actor.walking=false;actor.animationTime+=dt;
        actor.camera=enterTriggers(scene.triggers,old,actor.membership,actor.camera,scene.triggerGrid);
        return {};
    }
    if(actor.clearanceRadius<0)actor.clearanceRadius=scene.radius;
    // Native42B3D0 tests authored P1 walls, not the exporter-created floor seams.
    std::vector<GroundPolygon> playerGround;
    if(sourcePose)for(const auto& polygon:scene.ground) {
        if(!polygon.boundaryOnly&&(polygon.sourceIndex>=scene.queries.volumes.size()||
           scene.queries.volumes[polygon.sourceIndex].geometry.kind!=2||
            (scene.queries.volumes[polygon.sourceIndex].flags&2)))playerGround.push_back(polygon);
    }
    const auto& staticGround=sourcePose?playerGround:scene.ground;
    const Vec2 requested=velocity*(scene.speed*(run?2.0f:1.0f)*dt);
    const bool scriptedOutside=sourcePose&&(!floorHeight(scene.floor,old)||!clearGround(staticGround,old,0));
    std::vector<GroundPolygon> solids;
    if(scriptedOutside||!dynamic.empty()) {
        for(const auto& polygon:staticGround)if(!scriptedOutside||!polygon.boundaryOnly)solids.push_back(polygon);
        for(auto obstacle:dynamic) {
            if(sourcePose&&obstacle.circleRadius>0)obstacle.circleRadius+=scene.radius;
            solids.push_back(std::move(obstacle));
        }
    }
    Vec2 next=moveGround(scriptedOutside||!dynamic.empty()?solids:staticGround,old,requested,actor.clearanceRadius);
    if(sourcePose&&!scriptedOutside&&!floorHeight(scene.floor,next)) {
        // Keep floor-edge sliding without making internal triangle seams into walls.
        auto floorSolids=dynamic.empty()?staticGround:solids;
        for(const auto& polygon:scene.ground)if(polygon.boundaryOnly)floorSolids.push_back(polygon);
        next=moveGround(floorSolids,old,requested,actor.clearanceRadius);
        if(!floorHeight(scene.floor,next))next=old;
    }
    // A moving NPC can overlap a valid player pose beside a narrowing wall gap.
    // If combined recovery cannot fit both, block this manual step at the last
    // static-safe pose; source simulation continues without changing the radius.
    if(sourcePose&&!dynamic.empty()&&!scriptedOutside&&
       !clearGround(staticGround,next,actor.clearanceRadius)&&clearGround(staticGround,old,actor.clearanceRadius))next=old;
    if (!scriptedOutside&&!clearGround(staticGround,next,actor.clearanceRadius)) throw std::runtime_error("Collision resolver left the walkable floor");
    if(!sourcePose&&clearGround(staticGround,next,0)&&(scriptedOutside||actor.clearanceRadius<scene.radius)) {
        float radius=initialActorRadius(scene,next);
        for(const auto& polygon:dynamic)if(polygon.enabled)radius=std::min(radius,std::max(0.0f,nearestBoundary(polygon,next).distance-ContactSkin));
        actor.clearanceRadius=scriptedOutside?radius:std::max(actor.clearanceRadius,radius);
    }
    const auto height=floorHeight(scene.floor,next,&actor.floorTriangle);
    if (!height&&!scriptedOutside) throw std::runtime_error("Collision resolver left the original floor triangles");
    actor.position[0]=next.x; if(height)actor.position[1]=*height+actor.floorOffset; actor.position[2]=next.z;
    const float moved=length(next-old);
    const bool walking=moved>0.000001f;
    if (walking) actor.yaw=std::atan2(next.x-old.x,next.z-old.z);
    else if (length(velocity)>0.001f) actor.yaw=std::atan2(velocity.x,velocity.z);
    if (walking!=actor.walking) actor.animationTime=0;
    actor.animationTime+=walking?moved/scene.speed:dt;
    actor.walking=walking;
    actor.camera=enterTriggers(scene.triggers,next,actor.membership,actor.camera,scene.triggerGrid);
    return {length(requested),moved,requested};
}
std::array<float, 16> multiply(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> result{};
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

#include "graphics.h"

struct DoorAudio {
    SDL_AudioSpec spec{};
    std::vector<Uint8> pcm;
    SDL_AudioStream* stream=nullptr;
    double duration=0,remaining=0,playbackDuration=0;
    Uint64 started=0;
    explicit DoorAudio(const fs::path& file) {
        Uint8* bytes=nullptr; Uint32 length=0;
        checked(SDL_LoadWAV(file.u8string().c_str(),&spec,&bytes,&length),"Read original door sound");
        try { pcm.assign(bytes,bytes+length); } catch (...) { SDL_free(bytes); throw; }
        SDL_free(bytes);
        if (!spec.freq||!spec.channels||!SDL_AUDIO_FRAMESIZE(spec)||!length) throw std::runtime_error("Invalid original door sound");
        duration=double(length)/(double(spec.freq)*SDL_AUDIO_FRAMESIZE(spec));
    }
    ~DoorAudio() { if (stream) SDL_DestroyAudioStream(stream); }
    void openDevice() {
        if(stream)return;
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Audio unavailable: %s",SDL_GetError()); return; }
        stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if (stream&&!SDL_ResumeAudioStreamDevice(stream)) { SDL_DestroyAudioStream(stream); stream=nullptr; }
        if (!stream) SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Audio unavailable; retaining source sound duration: %s",SDL_GetError());
    }
    void play() {
        remaining=duration;playbackDuration=duration;started=SDL_GetTicksNS();
        if (stream&&(!SDL_ClearAudioStream(stream)||!SDL_PutAudioStreamData(stream,pcm.data(),int(pcm.size()))))
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Door sound playback failed: %s",SDL_GetError());
    }
    void advance(float dt) { remaining=std::max(0.0,remaining-dt); }
    bool playing() const {
        if (!stream) return remaining>0;
        return (started&&double(SDL_GetTicksNS()-started)/1e9<playbackDuration)||SDL_GetAudioStreamQueued(stream)>0;
    }
};
fs::path worldScript(const fs::path& directory) {
    std::ifstream input(directory/"world.txt");std::string magic,first;input>>magic>>first;
    if(!input||magic!="EDWORLD1"||!validSceneId(first)||(first[0]!='a'&&first[0]!='b'))
        throw std::runtime_error("Invalid world script package identity");
    return directory/"scripts"/(first[0]=='b'?"levelB.jpc":"levelA.jpc");
}
struct World;
std::unique_ptr<World> stageSavedWorld(World& current,const std::vector<uint8_t>& bytes);
struct World {
    static constexpr uint32_t SaveVersion=20;
    using MoviePresenter=std::function<void(World&,uint32_t,int)>;
    fs::path root;
    std::vector<std::string> areas;
    Scene scene;
    Actor actor;
    std::vector<GateState> gates;
    DoorAudio audio;
    AudioBank audioBank;
    std::unique_ptr<StreamAudio> streams;
    std::unique_ptr<edcamera::Timeline> cameraTimeline;
    ScriptGame game;
    std::unique_ptr<edactors::GameplayActors> actors;
    std::unique_ptr<SourceActorRoutes> routes;
    std::unique_ptr<edvfx::SourceVfx> vfx;
    int32_t sourceFloorCache=-1;
    SourceFloorQuery::CoarseGrid sourceFloorGrid{};
    bool originalRuntime=false,levelBPackage=false;
    bool scriptFault=false;
    bool overlayClockPaused=false;
    bool modernAimHeld=false;
    Vec2 modernFacing{};
    Movement modernFacingMovement;
    unsigned nativeDirections=0;
    unsigned sampledNativeDirections=0;
    bool nativeInputReset=false;
    unsigned nativeActionsHeld=0;
    std::array<unsigned,2> nativeTurnHeld{};
    int8_t nativeTurnX=0;
    ActorStep nativeMovementStep;
    ednativeinput::Queue nativeInputQueue;
    bool nativeUseSubmitted=false,nativeAimCancelled=false,nativeModeChanged=false;
    bool visibilityReady=false;
    unsigned visibilityCamera=0;
    InventoryView inventoryView;
    unsigned guard=0,transitions=0,attempts=0;
    uint64_t sourceTick=0;
    double tickFraction=0;
    double rawClockSeconds=0;
    int pending=-1,failedPortal=-1,noticeDoor=-1;
    std::string notice;
    std::ostream* trace=nullptr;
    MoviePresenter moviePresenter;
    edrumble::Host rumbleHost;
    std::optional<PackageRequest> pendingSceneSwitch;
    bool quitRequested=false;
    edrumble::Chainsaw chainsawFeedback() const {
        edrumble::Chainsaw result;result.gameplay=false;
        if(!originalRuntime||!game.profile.dreamcast)return result;
        const auto& method=game.package.methods.at(size_t(game.package.methodIndex("RumbleCommander","startRumble","(IIIIII)V")));
        const size_t code=56+method.offset;
        if(!method.hasCode||method.length<3||game.package.data.at(code)!=0xb2)throw jpc::Error("Original rumble option operand is unavailable");
        const auto field=game.package.constant(method.owner,(unsigned(game.package.data.at(code+1))<<8)|game.package.data.at(code+2)).bits;
        result.enabled=game.vm.staticField(field).integer()!=0;
        result.gameplay=actors&&!scene.menuContext&&!scriptFault&&game.mode==0&&game.vm.staticField(844).integer()==0&&game.vm.field(game.ash,13).integer()>0;
        const auto saw=game.vm.field(game.ash,102).reference();if(!saw)return result;
        const int type=game.vm.field(saw,812).integer();if(type<100||type>102)return result;
        result.weapon=uint32_t(saw);result.phase=game.vm.field(saw,139).integer();
        result.running=game.vm.field(saw,157).integer()!=0&&result.phase>0&&result.phase<4;
        result.revving=(nativeActionsHeld&4u)!=0;
        result.revTimer=game.vm.field(saw,162).integer();result.attackPitch=game.vm.field(saw,166).integer();
        return result;
    }
#include "episode_world.inl"
#include "world_actors.inl"
#include "episode_progress.inl"
#include "world_cart.inl"
#include "source_menu.inl"
#include "world_text.inl"
#include "inventory_combine.inl"
#include "world_screen.inl"
#include "world_package.inl"
#include "world_save.inl"
#include "debug_world.inl"
    explicit World(const fs::path& directory,std::ostream* output=nullptr,MoviePresenter presentation={},bool original=false,const PackageArrival* arrival=nullptr,bool startAtTitle=false,edrumble::Host feedback={}):root(directory),audio(directory/"door.wav"),
        audioBank(directory,original),game(worldScript(directory),!original),originalRuntime(original),
        levelBPackage(std::any_of(game.package.classes.begin(),game.package.classes.end(),[](const auto& type){return type.name=="LevelB";})),
        trace(output),moviePresenter(std::move(presentation)),rumbleHost(std::move(feedback)) {
        std::ifstream input(root/"world.txt"); std::string magic,first; unsigned count=0;
        input>>magic>>first>>count;
        if (!input||magic!="EDWORLD1"||!validSceneId(first)||!count||count>256) throw std::runtime_error("Invalid world manifest");
        for (unsigned i=0;i<count;++i) {
            std::string id; input>>id;
            if (!input||!validSceneId(id)||std::find(areas.begin(),areas.end(),id)!=areas.end()) throw std::runtime_error("Invalid world area identity");
            areas.push_back(id);
        }
        if(startAtTitle) {
            if(!originalRuntime||arrival)throw jpc::Error("Initial title requires a fresh original context");
            first=levelBPackage?"bb01":"aa01"; // ed.ini stream=aa01; level0/level1 are separate gameplay starts.
        }
        if(arrival)first=arrival->scene;
        if(!containsScene(first))throw std::runtime_error("Initial area is missing");
        scene=loadScene(root/first);
        if (scene.id!=first) throw std::runtime_error("Scene identity does not match world directory");
        actor=resetActor(scene,0); gates.resize(scene.doors.size());
        if(originalRuntime&&fs::is_regular_file(root/"gui/uv-ordinals.bin"))sourceGui.loadUVs(root/"gui/uv-ordinals.bin");
        bindScripts();
        if(originalRuntime) {
            if(!scene.episode)throw jpc::Error("Original gameplay requires the complete episode assets");
            streams=std::make_unique<StreamAudio>(root,game.profile.dreamcast);
            if(scene.cameraFrames.empty())throw jpc::Error("Original gameplay requires exported camera frames");
            cameraTimeline=std::make_unique<edcamera::Timeline>(scene.cameraTracks,episode_portal_detail::sceneId(scene.id));
            initializeVfx();initializeActors();initializeRoutes();
            if(arrival)preparePackageArrival(*arrival);
            initializeEpisodeProgress();
            if(arrival)restorePackagePlayer(*arrival);
            actors->captureLevelBoundary();
            if(arrival)enterPackageArrival(*arrival);else enterSourceScene(scene,actor,gates,true);
        } else {if(scene.episode)game.vm.invoke(game.package.methodIndex("ObjDB","initialize","()V"));enterSourceScene(scene,actor,gates);}
        if (trace) *trace<<"event,source_tick,active_scene,source_scene,portal_index,destination,enabled,pending,elapsed,guard,voice,x,y,z,yaw,camera_index,active_enabled,active_pending\n"<<std::setprecision(9);
        record("activate");
    }
    fs::path directory() const { return root/scene.id; }
    bool containsScene(const std::string& id)const {
        return std::find(areas.begin(),areas.end(),id)!=areas.end()||
            ((id=="aa01"||id=="bb01")&&fs::is_regular_file(root/id/"menu.txt"));
    }
    void bindScripts() {
        auto bind=[&](const std::string& name,jpc::Interpreter::Native callback) {
            const auto found=std::find_if(game.package.methods.begin(),game.package.methods.end(),[&](const auto& method){
                return game.package.classes[size_t(method.owner)].name=="ed/base/External"&&method.name==name;
            });
            if(found==game.package.methods.end())throw jpc::Error("Missing world native: "+name);
            game.vm.bind("ed/base/External",name,found->signature,std::move(callback));
        };
        bind("actorInTrigger",[this](auto&,auto,const auto& args){
            return jpc::Value::integer(nativeScene().scripting.inside(unsigned(args[2].integer()),scriptActorPosition(args)));
        });
        bind("actorIsFacingMarker",[this](auto&,auto,const auto& args){
            const auto placed=scriptActorPosition(args);
            return jpc::Value::integer(nativeScene().scripting.facing(unsigned(args[2].integer()),float(args[3].integer()),
                {placed.x,placed.z},scriptActorYaw(args)));
        });
        bind("sndMgrGetSNDID",[](auto&,auto,const auto& args){return args[0];});
        bind("sndMgrPlaySoundVol",[this](auto&,auto,const auto& args){
            if(uint32_t(args[0].integer())!=0x0e8a4aca)throw jpc::Error("Original sound has not been exported yet");
            audio.play();return jpc::Value::integer(1);
        });
        bind("sndMgrIsVoiceDone",[this](auto&,auto,const auto&){return jpc::Value::integer(!audio.playing());});
        bind("getMilliseconds",[this](auto&,auto,const auto&){
            return jpc::Value::integer(jpc::signedBits(uint32_t(uint64_t(packageClockNow()*1000))));
        });
        bindEpisodeScripts(bind);
        bindScreenNatives(bind);
        bindCartNatives(bind);
        bind("sceneSwitch",[this](auto&,auto,const auto& args){
            PackageRequest request{episode_portal_detail::sceneId(nativeScene().id),uint32_t(args[0].integer()),
                uint32_t(args[1].integer()),args[2].integer(),{{0,float(double(args[3].integer())*0.01745329238474369f),0}}};
            edpackage::sceneName(request.destinationId);
            if(request.camera>255||request.marker< -1||request.marker>255)throw jpc::Error("Invalid original sceneSwitch camera/marker");
            if(request.destinationId==request.sourceSceneId)applySameSceneTransition(request);
            else {pendingSceneSwitch=request;if(actors)actors->clearMessages();}
            return jpc::Value{};
        });
        if(levelBPackage)bind("reboot",[this](auto&,auto,const auto&){
            pendingSceneSwitch=PackageRequest{episode_portal_detail::sceneId(nativeScene().id),0x62620100u,0,-1,{{0,0,0}}};
            return jpc::Value{};
        });
        bind("systemAbort",[this](auto&,auto,const auto&){quitRequested=true;return jpc::Value{};});
        bind("switchToDisk",[](auto&,auto,const auto&){return jpc::Value::integer(1);}); //418c60 is literal return1 on PC.
        if(!game.profile.dreamcast)bind("guiKeyboardConfigAction",[](auto&,auto,const auto& args){
            const int action=args[0].integer();
            // SDL selects menu/game mappings by context; the original DIK capture UI is replaced by ControlMenu.
            if(action==1||action==2||action==5||action==6||action==7)return jpc::Value::integer(0);
            throw jpc::Error("Original keyboard capture must use the modern controls popup");
        });
        if(originalRuntime)bindEpisodeProgress(bind);
    }
    void syncInventory() {
        inventoryView.open=game.mode==2;inventoryView.items.clear();
        inventoryView.health=game.vm.field(game.ash,13).integer();inventoryView.maxHealth=game.vm.field(game.ash,95).integer();
        for(int container:{99,98,100})if(container!=100||originalRuntime)
            for(const auto& item:game.items(container))inventoryView.items.push_back({item.itemID,item.quantity,item.slot,container});
        if(inventoryView.items.empty())inventoryView.selected=0;
        else inventoryView.selected=std::min(inventoryView.selected,unsigned(inventoryView.items.size()-1));
        if(!inventoryView.open||inventoryView.items.empty()||inventoryView.items[inventoryView.selected].containerField!=100)closeDocument();
        if(!inventoryView.open||inventoryView.reading||std::none_of(inventoryView.items.begin(),inventoryView.items.end(),[&](const auto& item){
            return item.containerField==99&&item.slot==inventoryView.combineSlot&&item.itemID==inventoryView.combineItemID;}))clearInventoryCombination();
    }
    void setInterfaceMode(int mode) {
        if(!originalRuntime){game.mode=mode;syncInventory();return;}
        const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();const Actor previousActor=actor;beginNativeStage();
        try {changeEpisodeInterfaceMode(mode);syncInventory();commitNativeStage();}
        catch(const jpc::Error& error) {
            game.rollback(before);rollbackNative(nativeBefore);actor=previousActor;syncInventory();
            notice="could not change menu";noticeDoor=-1;SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Menu retained its previous state: %s",error.what());
        }
    }
    void closeInventory() {setInterfaceMode(0);}
    void setOverlayPaused(bool paused) {
        if(!originalRuntime)return;
        if(paused&&!overlayClockPaused&&game.mode==0) {
            if(!game.vm.staticField(48).integer())game.vm.invoke(game.package.methodIndex("ed/base/EDSystem","pauseTime","()V"));
            overlayClockPaused=true;
        } else if(!paused&&overlayClockPaused) {
            if(game.mode==0)game.vm.invoke(game.package.methodIndex("ed/base/EDSystem","resumeTime","()V"));
            overlayClockPaused=false;
        }
    }
    void advanceIdleTime(double dt){
        rawClockSeconds+=dt;audio.advance(float(dt));audioBank.advance(dt);advanceStream(dt);
        for(auto voice=nativeProperties.voices.begin();voice!=nativeProperties.voices.end();)
            if(voice->first>=0x10000&&audioBank.done(voice->first))voice=nativeProperties.voices.erase(voice);else ++voice;
    }
    void sampleNativeController(const ControlFrame& input) {
        if(!game.profile.dreamcast)return;
        sampledNativeDirections=(input.held(ControlAction::Forward)?1u:0u)|(input.held(ControlAction::Backward)?2u:0u)|
            (input.held(ControlAction::TurnLeft)?4u:0u)|(input.held(ControlAction::TurnRight)?8u:0u);
        nativeTurnX=input.nativeTurnX;
        modernFacing=input.held(ControlAction::Aim)&&length(input.movement)<=.001f?input.facing:Vec2{};
        if(scene.menuContext||game.mode!=0||game.vm.staticField(844).integer()!=0||overlayClockPaused){nativeActionsHeld=0;modernFacing={};modernFacingMovement={};}
    }
    bool enqueueNativeInput(int action,unsigned mask,bool pressed=true) {
        return nativeInputQueue.append(ednativeinput::physical(action,mask,pressed));
    }
    bool requestPlayerInterface(int mode) {
        if(mode!=2&&mode!=3)throw std::runtime_error("Invalid player interface request");
        if(!game.profile.dreamcast){setInterfaceMode(mode);return true;}
        return enqueueNativeInput(mode==2?112:111,mode==2?0x2000:0x1000,true);
    }
    void resetNativeController() {
        modernAimHeld=false;nativeDirections=sampledNativeDirections=nativeActionsHeld=0;
        modernFacing={};modernFacingMovement={};
        nativeTurnHeld={};nativeTurnX=0;nativeInputReset=true;nativeMovementStep={};
        nativeInputQueue.clear();nativeUseSubmitted=nativeAimCancelled=nativeModeChanged=false;
    }
    bool takeNativeAimCancellation(){const bool value=nativeAimCancelled;nativeAimCancelled=false;return value;}
    bool takeNativeModeChange(){const bool value=nativeModeChanged;nativeModeChanged=false;return value;}
    void playerControls(const ControlFrame& input,bool physicalInput=true) {
        if(physicalInput)sampleNativeController(input);
        if(!game.profile.dreamcast) {
            const bool physicalAim=physicalInput;

        debugResources();
        if(!actors||scene.menuContext||scriptFault||game.vm.staticField(844).integer()!=0||(actors->at({0,0}).flags&0x48))return;
        const bool aiming=input.held(ControlAction::Aim);
        const bool armed=game.vm.field(game.ash,123).integer()!=0;
        // Original A/X/LT ready weapons themselves; only an explicit Aim release holsters them.
        const bool changeAim=physicalAim&&(aiming?!armed:modernAimHeld&&armed);
        if(physicalAim)modernAimHeld=aiming;
        const bool wantedMovement=length(input.movement)>.001f;
        // Modern held intent reconciles source3/4 metadata; defer guarded3
        // without an input heartbeat, and leave native locomotion to step().
        const bool changeMovement=wantedMovement!=(game.vm.field(game.ash,124).integer()!=0)&&
            (!wantedMovement||game.vm.staticField(394).integer()==0);
        const bool rev=input.pressed(ControlAction::ChainsawRev),stop=input.pressed(ControlAction::ChainsawStop);
        if(!changeMovement&&!changeAim&&!rev&&!stop&&!input.pressed(ControlAction::PrimaryAttack)&&!input.pressed(ControlAction::SecondaryAttack)&&!input.pressed(ControlAction::Taunt))return;
        const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
        try {
            if(changeMovement) {
                actors->sync();
                game.vm.invoke(game.package.methodIndex("Ash","processPadAction","(I)I"),game.ash,
                    {jpc::Value::integer(wantedMovement?3:4)});
            }
            // A first original17 only produces Raise22 and stamps125; it
            // cannot consume ammo/fuel or attack while source123 is false.
            if(changeAim)actors->playerAction(aiming?17:23);
            if(stop)actors->playerAction(108);
            else if(rev)actors->playerAction(110);
            if(input.pressed(ControlAction::PrimaryAttack))actors->playerAction(15);
            if(input.pressed(ControlAction::SecondaryAttack))actors->playerAction(17);
            if(input.pressed(ControlAction::Taunt))actors->playerAction(19);
            debugResources();syncInventory();commitNativeStage();
        } catch(const jpc::Error& error) {
            game.rollback(before);rollbackNative(nativeBefore);notice="could not perform action";noticeDoor=-1;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Player action retained previous state: %s",error.what());
        }
    
            return;
        }
        debugResources();
        if(!actors||scene.menuContext||scriptFault||game.vm.staticField(844).integer()!=0||(actors->at({0,0}).flags&0x48))return;
        const auto& actions=actors->at({0,0}).actions;
        const unsigned oldDirections=nativeDirections;
        if(physicalInput)nativeDirections=sampledNativeDirections;
        const bool aiming=input.held(ControlAction::Aim),armed=game.vm.field(game.ash,123).integer()!=0;
        const bool aimPending=std::any_of(nativeInputQueue.records.begin(),nativeInputQueue.records.end(),[](const auto& r){return r.code==0xffff&&r.action==17;});
        if(physicalInput) {
            if(aiming&&!armed&&!aimPending)nativeInputQueue.append({17,0xffff,true});
            else if(!aiming&&modernAimHeld&&(armed||aimPending))nativeInputQueue.append({23,0xffff,false});
            modernAimHeld=aiming;
        }
        const unsigned down=(input.held(ControlAction::PrimaryAttack)?1u:0u)|(input.held(ControlAction::SecondaryAttack)?2u:0u)|(input.held(ControlAction::ChainsawRev)?4u:0u);
        const unsigned pressed=(input.pressed(ControlAction::PrimaryAttack)?1u:0u)|(input.pressed(ControlAction::SecondaryAttack)?2u:0u)|(input.pressed(ControlAction::ChainsawRev)?4u:0u);
        const unsigned released=physicalInput?((nativeActionsHeld|pressed)&~down):0;
        if(physicalInput)nativeActionsHeld=down;
        if(physicalInput) {
            if(pressed&1)enqueueNativeInput(15,1);
            if(released&1)enqueueNativeInput(16,1,false);
            if(input.pressed(ControlAction::Use)){enqueueNativeInput(140,2);nativeUseSubmitted=true;}
            if(pressed&2)enqueueNativeInput(17,4);
            if(released&2)enqueueNativeInput(18,4,false);
            if(input.pressed(ControlAction::Taunt))enqueueNativeInput(19,8);
        }
        const bool reset=nativeInputReset&&!game.vm.staticField(394).integer();
        for(const auto& direction:std::array<std::array<int,4>,2>{{{{int(ControlAction::TurnLeft),4,7,8}},{{int(ControlAction::TurnRight),8,9,10}}}}) {
            const unsigned bit=unsigned(direction[1]),mask=bit==4?0x10u:0x20u;
            if(physicalInput&&input.pressed(ControlAction(direction[0])))enqueueNativeInput(direction[2],mask);
            if((physicalInput&&!(nativeDirections&bit)&&((oldDirections&bit)||input.pressed(ControlAction(direction[0]))))||
               (reset&&!(nativeDirections&bit)&&actions.possible(direction[3])))enqueueNativeInput(direction[3],mask,false);
        }
        bool forward=game.vm.field(game.ash,124).integer()!=0,runKnown=game.vm.field(game.ash,121).integer()!=0,pendingForward=false;
        for(const auto& record:nativeInputQueue.records){if(record.action==3)forward=pendingForward=true;else if(record.action==4)forward=pendingForward=false;
            else if(record.action==24)runKnown=true;else if(record.action==25)runKnown=false;}
        const bool wanted=!(nativeDirections&2)&&(length(input.movement)>.001f||(nativeDirections&1));
        const bool backward=physicalInput&&input.pressed(ControlAction::Backward);
        const bool guard=game.vm.staticField(394).integer()!=0;
        // Source124 records intent even when the pickup/attack FSM rejects3.
        const bool movementChange=wanted?!guard&&!pendingForward&&(!forward||!edlocomotion::manualAction(actions))&&
            actions.possible(input.held(ControlAction::Run)?145:3):forward;
        const bool forwardEdge=physicalInput&&input.pressed(ControlAction::Forward);
        const bool forwardRelease=physicalInput&&!(nativeDirections&1)&&((oldDirections&1)||forwardEdge);
        if(forwardEdge)enqueueNativeInput(3,0x40);
        if(forwardRelease)enqueueNativeInput(4,0x40,false);
        else if(!((nativeDirections|oldDirections)&1)&&!forwardEdge&&movementChange)enqueueNativeInput(wanted?3:4,0x40,wanted);
        else if(reset&&!wanted&&edlocomotion::manualAction(actions)&&!std::any_of(nativeInputQueue.records.begin(),nativeInputQueue.records.end(),[](const auto& r){return r.action==4;}))enqueueNativeInput(4,0x40,false);
        if(backward)enqueueNativeInput(5,0x80);
        if((physicalInput&&!(nativeDirections&2)&&((oldDirections&2)||backward))||
           (reset&&!(nativeDirections&2)&&edlocomotion::backwardActive(actions)&&actions.possible(6)))enqueueNativeInput(6,0x80,false);
        if(physicalInput&&input.pressed(ControlAction::ChainsawStop))enqueueNativeInput(108,0x200);
        const bool run=input.held(ControlAction::Run);if(run!=runKnown)enqueueNativeInput(run?24:25,0x400,run);
        if(physicalInput){
            if(pressed&4)enqueueNativeInput(110,0x800);
            if(released&4)enqueueNativeInput(171,0x800,false);
            if(input.pressed(ControlAction::Pause))requestPlayerInterface(3);
            if(input.pressed(ControlAction::Inventory))requestPlayerInterface(2);
        }
        if(reset)nativeInputReset=false;
    }
    void processNativeInputFrame() {
        if(nativeInputQueue.records.empty()||(actors&&(actors->at({0,0}).flags&0x40)))return;
        nativeInputQueue.compact();
        const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
        try {
            bool more=true;
            while(more&&!nativeInputQueue.records.empty()) {
                const auto record=nativeInputQueue.pop();more=ednativeinput::direction(record);
                const int mode=game.mode;
                if(!scene.menuContext&&game.vm.staticField(844).integer()==0&&actors) {
                    const bool armed=game.vm.field(game.ash,123).integer()!=0;
                    const int chosen=game.vm.invoke(game.package.methodIndex("ActorDB","processPadAction","(III)I"),0,
                        {jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(record.action)}).integer();
                    if(chosen==111||chosen==112){changeEpisodeInterfaceMode(chosen==112?2:3);actor.movement.held=false;}
                    else if(chosen>0)actors->consoleAction({0,0},chosen);
                    if(record.action==140){
                        if(armed&&!game.vm.field(game.ash,123).integer())nativeAimCancelled=true;
                        finishSourceUse(chosen);
                    }
                } else sourceGui.dispatchInput(game.vm,{record.action});
                nativeModeChanged|=game.mode!=mode;
            }
            debugResources();syncInventory();commitNativeStage();
        }catch(const jpc::Error& error){game.rollback(before);rollbackNative(nativeBefore);nativeAimCancelled=nativeModeChanged=false;
            notice="could not perform action";noticeDoor=-1;SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Player input retained previous state: %s",error.what());}
    }
    void openInventory() {if(!scene.menuContext){setInterfaceMode(2);actor.movement.held=false;}}
    void clearInventoryCombination() {inventoryView.combineSlot=inventoryView.combineItemID=-1;}
    void markInventoryCombination() {
        if(!inventoryView.open||inventoryView.reading||inventoryView.items.empty())return;
        const auto& item=inventoryView.items[std::min<size_t>(inventoryView.selected,inventoryView.items.size()-1)];
        if(item.containerField!=99){notice="choose an inventory item";noticeDoor=-1;return;}
        if(inventoryView.combineSlot==item.slot&&inventoryView.combineItemID==item.itemID)clearInventoryCombination();
        else {inventoryView.combineSlot=item.slot;inventoryView.combineItemID=item.itemID;}
    }
    void applyInventoryItem() {
        if(inventoryView.items.empty())return;
        if(inventoryView.combineSlot>=0) {
            const auto selected=inventoryView.items[std::min<size_t>(inventoryView.selected,inventoryView.items.size()-1)];
            const auto live=game.items(99);
            if(std::none_of(live.begin(),live.end(),[&](const auto& item){return item.slot==inventoryView.combineSlot&&item.itemID==inventoryView.combineItemID;})) {
                clearInventoryCombination();notice="first item changed; choose again";noticeDoor=-1;return;
            }
            if(selected.containerField!=99){notice="choose an inventory item";noticeDoor=-1;return;}
            const int result=combineInventorySlots(inventoryView.combineSlot,selected.slot);
            notice=result>=0?"items combined":"items do not combine";noticeDoor=-1;
            if(result>=0)clearInventoryCombination();return;
        }
        const auto previous=game.checkpoint();
        const auto nativeBefore=checkpointNative();beginNativeStage();
        try {const auto& item=inventoryView.items.at(std::min<size_t>(inventoryView.selected,inventoryView.items.size()-1));
            if(item.containerField==100){readDocument(item);syncInventory();}
            else {game.useSlot(item.slot,item.containerField);scriptEffects();}
            commitNativeStage();}
        catch(const jpc::Error& error){game.rollback(previous);rollbackNative(nativeBefore);syncInventory();notice="action is not connected yet";noticeDoor=-1;SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Item use: %s",error.what());}
    }
    bool scriptEffects() {
        syncInventory();
        if(!game.vm.staticField(game.hostField(HostFieldRole::ScenePendingPortal)).integer())return false;
        const int portal=game.vm.staticField(game.hostField(HostFieldRole::ScenePortalIndex)).integer();
        const auto found=std::find_if(scene.doors.begin(),scene.doors.end(),[&](const auto& door){return int(door.portalIndex)==portal;});
        if(found==scene.doors.end()||(!scene.episode&&found->interaction.points.empty()))throw jpc::Error("Script requested a passage outside the loaded world");
        const int index=int(found-scene.doors.begin());
        if(pending>=0)gates[pending].pending=false;
        pending=index;gates[index].pending=true;gates[index].elapsed=0;
        guard=unsigned(game.vm.staticField(395).integer());failedPortal=-1;notice.clear();noticeDoor=-1;
        record("use",index);return true;
    }
    void writeTrace(const char* event,const std::string& source,int portal,const std::string& target,GateState state) {
        if (!trace) return;
        unsigned enabled=0,opening=0;
        for (const auto& gate:gates) { enabled+=gate.enabled; opening+=gate.pending; }
        *trace<<event<<','<<sourceTick<<','<<scene.id<<','<<source<<','<<portal<<','<<target<<','<<state.enabled<<','
              <<state.pending<<','<<state.elapsed<<','<<guard<<','<<sourceVoicePlaying()<<','<<actor.position[0]<<','<<actor.position[1]<<','
              <<actor.position[2]<<','<<actor.yaw<<','<<actor.camera<<','<<enabled<<','<<opening<<'\n';
    }
    void record(const char* event,int index=-1) {
        if (index>=0) writeTrace(event,scene.id,int(scene.doors[index].portalIndex),scene.doors[index].destination,gates[index]);
        else writeTrace(event,scene.id,-1,scene.id,{});
    }
    void fixture(Vec2 position,float yaw,int doorIndex=-1) {
        Actor placed;
        const auto height=floorHeight(scene.floor,position,&placed.floorTriangle);
        if (!height||!clearGround(scene.ground,position,scene.radius)) throw std::runtime_error("Test fixture is not on original walkable floor");
        placed.position[0]=position.x; placed.position[1]=*height; placed.position[2]=position.z; placed.yaw=yaw;
        placed.clearanceRadius=scene.radius;
        placed.membership.assign(scene.triggers.size(),false);
        placed.camera=enterTriggers(scene.triggers,position,placed.membership,0,scene.triggerGrid);
        actor=std::move(placed); failedPortal=-1; notice.clear(); noticeDoor=-1;
        record("fixture",doorIndex);
    }
    int nearestDoor(bool facingRequired) const {
        int nearest=-1; float distance=std::numeric_limits<float>::max();
        const Vec2 position{actor.position[0],actor.position[2]};
        for (size_t i=0;i<scene.doors.size();++i) {
            const auto& door=scene.doors[i];
            if (door.interaction.points.empty()) {
                if (facingRequired||!insideDoorRegion(door.portal,scene.triggerGrid,position)) continue;
            } else if (!insideTrigger(door.interaction,position)||(facingRequired&&!facingDoor(door,position,actor.yaw))) continue;
            const float current=length(Vec2{door.marker.x,door.marker.z}-position);
            if (current<distance) { distance=current; nearest=int(i); }
        }
        return nearest;
    }
    void finishSourceUse(int action) {
        if(!scriptEffects()&&!action) {
            const int index=nearestDoor(false);
            if(index>=0&&scene.doors[index].interaction.points.empty())record("unsupported",index);
            else record("rejected",index);
        }
    }
    void use() {
        if(scene.menuContext||scriptFault||(actors&&(game.vm.staticField(844).integer()!=0||(actors->at({0,0}).flags&0x48))))return;
        int index=nearestDoor(true);
        if (guard&&!game.profile.dreamcast) { record("rejected",index); return; }
        const auto previous=game.checkpoint();
        const auto nativeBefore=checkpointNative();beginNativeStage();
        try {
            const int action=actors?actors->playerAction(140):game.interact();
            finishSourceUse(action);
            commitNativeStage();
        } catch(const jpc::Error& error) {
            game.rollback(previous);rollbackNative(nativeBefore);syncInventory();notice="action is not connected yet";noticeDoor=-1;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Interaction retained its previous state: %s",error.what());
        }
    }
    bool transfer(size_t index,Graphics* graphics) {
        if(index>=scene.doors.size())throw std::runtime_error("Transition index is outside current scene");
        const Door door=scene.doors[index];
        if ((!scene.episode&&door.interaction.points.empty())||!gates[index].enabled||
            !insideDoorRegion(door.portal,scene.triggerGrid,{actor.position[0],actor.position[2]})) return false;
        PackageRequest request{episode_portal_detail::sceneId(scene.id),episode_portal_detail::sceneId(door.destination),door.destinationCamera,0,{{0,door.yaw,0}}};
        if(scene.episode)for(const auto& portal:scene.portalData.portals)if(portal.index==door.portalIndex){request.marker=int32_t(portal.marker);request.rotationXYZ=portal.rotationXYZ;}
        return switchScene(request,graphics,int(index));
    }
    bool switchScene(const PackageRequest& request,Graphics* graphics,int index=-1) {
        Door door=index>=0?scene.doors.at(size_t(index)):Door{};
        door.destination=edpackage::sceneName(request.destinationId);door.destinationCamera=request.camera;
        ++attempts;record("attempt",index);
        const auto scriptBefore=game.checkpoint();
        const auto nativeBefore=checkpointNative();
        beginNativeStage();
        try {
            if(request.sourceSceneId!=episode_portal_detail::sceneId(scene.id)||request.camera>255||request.marker< -1||request.marker>255)
                throw jpc::Error("Invalid original scene transition request");
            if(originalRuntime&&edpackage::reload(request.sourceSceneId,request.destinationId)) {
                pendingSceneSwitch=request;commitNativeStage();return true;
            }
            const EpisodePortal* original=nullptr;
            if(scene.episode&&index>=0) {
                for(const auto& portal:scene.portalData.portals)if(portal.index==door.portalIndex)original=&portal;
                if(!original||!original->resolved)throw std::runtime_error("Original destination has not been exported yet");
            }
            if(!containsScene(door.destination))throw std::runtime_error("Destination is not included in this prototype");
            Scene next=loadScene(root/door.destination);
            if (next.id!=door.destination||door.destinationCamera>=next.cameras.size()) throw std::runtime_error("Destination identity or camera mismatch");
            if(index<0&&request.marker>=0) {
                if(size_t(request.marker)>=next.scripting.markers.size())throw jpc::Error("Original sceneSwitch marker is missing");
                const auto marker=next.scripting.markers[size_t(request.marker)];door.spawn={marker.x,0,marker.z};
                if(request.rotationXYZ[0]!=0||request.rotationXYZ[2]!=0)throw jpc::Error("Unsupported source player pitch/roll");
                door.yaw=edactormath::normalizeAngle(request.rotationXYZ[1]);
            }
            if(original) {
                if(original->marker>=next.scripting.markers.size())throw std::runtime_error("Original destination marker is missing");
                const auto marker=next.scripting.markers[original->marker];
                if(marker.x!=original->markerXYZ[0]||marker.y!=original->markerXYZ[1]||marker.z!=original->markerXYZ[2])
                    throw std::runtime_error("Original destination marker differs from resolved portal");
            }
            Actor arrival;
            const Vec2 position{door.spawn.x,door.spawn.z};
            const auto height=floorHeight(next.floor,position,&arrival.floorTriangle);
            if (!actors&&request.marker!=-1&&(!height||!clearGround(next.ground,position,next.radius))) throw std::runtime_error("Native arrival marker is not walkable");
            arrival.position[0]=position.x; arrival.position[1]=height.value_or(0)+door.spawn.y; arrival.position[2]=position.z;
            arrival.floorOffset=door.spawn.y; arrival.yaw=door.yaw; arrival.camera=door.destinationCamera;
            // Original activateScene may disable an arrival solid (AH20 marker3/volume18).
            arrival.clearanceRadius=request.marker==-1?0:actors?-1:next.radius;
            arrival.membership.assign(next.triggers.size(),false);
            std::vector<GateState> nextGates(next.doors.size());
            auto prepared=graphics?graphics->prepare(next,root/door.destination):nullptr;
            if(actors){sourceScreen.fadeOut();waitForScreenFade(graphics);}
            if(actors)endEpisodeScene();
            enterSourceScene(next,arrival,nextGates,false,request.marker!=-1);
            if(actors&&request.marker==-1){publishEpisodeCamera(request.camera);readNativePlayer(arrival,next,false);}
            if(graphics)graphics->prepareCameraFrame(*prepared,next,arrival.camera);
            const auto oldId=scene.id; const auto oldState=index>=0?gates[size_t(index)]:GateState{};
            if (graphics) graphics->commit(std::move(prepared));
            scene=std::move(next); actor=std::move(arrival); gates=std::move(nextGates);
            if(actors){screenQueuedPortal.reset();sourceScreen.fadeIn();}
            commitNativeStage();
            pending=failedPortal=noticeDoor=-1; notice.clear(); ++transitions;
            writeTrace("transition",oldId,int(door.portalIndex),scene.id,oldState);
            record("activate");
            return true;
        } catch (const std::exception& error) {
            game.rollback(scriptBefore);
            rollbackNative(nativeBefore);
            failedPortal=index; noticeDoor=index; notice="could not load area";
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Area load failed; current area retained: %s",error.what());
            record("failure",index);
            return false;
        }
    }
    ActorStep step(Vec2 stick,bool run,bool useEdge,float dt,Graphics* graphics=nullptr) {
        const bool canonical=actors&&game.profile.dreamcast;
        nativeMovementStep={};
        nativeAimCancelled=nativeModeChanged=false;
        if(canonical&&useEdge&&!nativeUseSubmitted)enqueueNativeInput(140,2);
        nativeUseSubmitted=false;
        if(dt>0)debugResources();
        advanceIdleTime(dt);
        if(scriptFault)return {};
        if(canonical&&!scene.menuContext) {
            const auto& body=actors->at({0,0});const auto& camera=scene.cameras[actor.camera];
            const Vec2 velocity=actor.movement.world(stick,camera.right,camera.forward);
            const Vec2 facing=modernFacingMovement.world(modernFacing,camera.right,camera.forward);
            const Vec2 heading=length(velocity)>.001f?velocity:facing;
            if(!(nativeDirections&14)&&game.vm.staticField(844).integer()==0&&!game.vm.staticField(394).integer()&&!(body.flags&0x48)&&!game.vm.field(game.ash,11).integer()&&
               (edlocomotion::manualAction(body.actions)||body.actions.possible(run?145:3))&&length(heading)>.001f)
                actor.yaw=std::atan2(heading.x,heading.z);
            if(dt==0)placeNativePlayer(actor);
        }
        if(dt>0&&canonical&&!(actors->at({0,0}).flags&0x40)) {
                constexpr unsigned ramp[]{0,0,0,1,2,4,5,6,7,8,8,8,8,8,8};
                for(unsigned side=0;side<2;++side) {
                    auto& held=nativeTurnHeld[side];held=(sampledNativeDirections&(4u<<side))?((held+1)&0x1fffu):0;
                    if(held)nativeProperties.turnAmount=held<15?int(ramp[held]):8;
                    else if(side==0?nativeTurnX< -25:nativeTurnX>25)nativeProperties.turnAmount=(((8*int(nativeTurnX)*int(nativeTurnX))>>14)*3)>>1;
                }
        }
        if(dt>0&&actors&&!scene.menuContext) {
            ControlFrame motion;motion.movement=stick;
            motion.down[size_t(ControlAction::Aim)]=game.vm.field(game.ash,123).integer()!=0;
            motion.down[size_t(ControlAction::Run)]=run;
            playerControls(motion,false);
        }
        if(canonical&&dt>0)processNativeInputFrame();
        // Native420DF1 accepts one iteration after >=33ms, without catch-up.
        tickFraction=originalRuntime?double(dt>0):tickFraction+double(dt)*30;
        while (tickFraction>=1) {
            tickFraction-=1; ++sourceTick;
            if(actors) {
                const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
                try {
                    placeNativePlayer(actor);processEpisodeFrame();
                    const int appliedMode=game.vm.staticField(844).integer();
                    if(appliedMode==0)routes->steerActors();
                    actors->update(sourceTick,appliedMode==0,appliedMode==0?0u:1u);
                    if(appliedMode==0){vfx->update(sourceTick);routes->update();}
                    if(game.mode!=game.vm.staticField(844).integer())changeEpisodeInterfaceMode(game.mode);
                    advanceCameraTimeline();
                    sampleEpisodeFloors();sampleEpisodeEffects();vfx->sampleRender(true);readNativePlayer(actor,scene);
                    guard=unsigned(game.vm.staticField(395).integer());
                    debugResources();syncInventory();sampleSourceTexts();commitNativeStage();
                } catch(const std::exception& error) {
                    game.rollback(before);rollbackNative(nativeBefore);--sourceTick;
                    scriptFault=true;game.diagnostic=error.what();notice="script error; save in pause menu";noticeDoor=-1;
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Original frame retained: %s",error.what());return {};
                }
                if(pending>=0) {
                    const int index=pending;++gates[index].elapsed;
                    if(!game.vm.staticField(game.hostField(HostFieldRole::ScenePendingPortal)).integer()){gates[index].pending=false;pending=-1;record("ready",index);}
                }
                continue;
            }
            if (guard) --guard;
            game.vm.statics[394]=jpc::Value::integer(guard?1:0);game.vm.statics[395]=jpc::Value::integer(int32_t(guard));
            if(scene.episode) {
                game.vm.statics[393]=jpc::Value::integer(jpc::signedBits(uint32_t(game.vm.staticField(393).integer())+1));
                game.vm.statics[398]=jpc::Value::integer(jpc::signedBits(uint32_t(sourceTick*1000/30)));
                const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
                try {game.vm.invoke(game.package.methodIndex("GenericScene","soundTrackProcessFrame","()V"));commitNativeStage();}
                catch(const jpc::Error& error) {
                    game.rollback(before);rollbackNative(nativeBefore);
                    const std::string reason=error.what();if(game.diagnostic!=reason)SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Source sound frame retained: %s",reason.c_str());
                    game.diagnostic=reason;
                }
                if(pending>=0) {
                    const int index=pending;++gates[index].elapsed;
                    if(!game.vm.staticField(game.hostField(HostFieldRole::ScenePendingPortal)).integer()) {gates[index].pending=false;pending=-1;record("ready",index);}
                }
                continue;
            }
            if (pending>=0) {
                const int index=pending; tickGate(gates[index],scene.doors[index],audio.playing());
                if (!gates[index].pending) { pending=-1;game.vm.statics[game.hostField(HostFieldRole::ScenePendingPortal)]=jpc::Value::integer(0);record("ready",index); }
            }
        }
        const Actor previousActor=actor;
        auto movementAction=[](const edactions::ActorActions& actions) {
            const auto* owner=&actions.layers[0];
            // Match rootMotion ownership order, but retain the logical layer
            // state for queued FSM commands (not its physical mode4 parent).
            for(unsigned layer=1;layer<7;++layer) {
                const auto& candidate=actions.layers[layer];
                if(candidate.state>=0&&candidate.playback>=0) {
                    const auto& mask=actions.states.at(size_t(candidate.state)).boneMask;
                    if(!mask.empty()&&mask[0]){owner=&candidate;break;}
                }
            }
            if(owner->state<0||owner->playback<0||size_t(owner->playback)>=actions.playbacks.size())return 0;
            const auto& state=actions.states.at(size_t(owner->state));
            if(state.mode!=1)return 0;
            if(state.animation==actions.states.at(11).animation)return 3;
            return state.animation==actions.states.at(13).animation?5:0;
        };
        if(actors&&!canonical) {
            const auto& body=actors->at({0,0});
            if(game.vm.staticField(844).integer()!=0||(body.flags&0x48)||game.vm.field(game.ash,11).integer()||
               (!movementAction(body.actions)&&!body.actions.possible(run?5:3)))stick={};
        }
        ActorStep moved;
        if(canonical) {
            moved=nativeMovementStep;
            actor.walking=edlocomotion::manualAction(actors->at({0,0}).actions)!=0&&moved.moved>.000001f;
            actor.animationTime+=dt;
            actor.camera=enterTriggers(scene.triggers,{actor.position[0],actor.position[2]},actor.membership,actor.camera,scene.triggerGrid);
        } else moved=stepActor(scene,actor,stick,run,dt,originalRuntime,
            actors&&length(stick)>0.001f?manualActorObstacles():std::vector<GroundPolygon>{});
        if(actors) {
            if(!canonical) {
                placeNativePlayer(actor);readNativePlayer(actor,scene);
                auto& body=actors->at({0,0});const int active=movementAction(body.actions),desired=run?5:3;
                if(active&&(!actor.walking||active!=desired))actors->consoleAction({0,0},active==5?6:4);
                if(actor.walking&&movementAction(body.actions)!=desired)actors->consoleAction({0,0},desired);
            }
            if(actor.camera!=previousActor.camera) {
                const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
                const auto camera=actor.camera;actor.camera=previousActor.camera;
                try {publishEpisodeCamera(camera);readNativePlayer(actor,scene);commitNativeStage();}
                catch(const std::exception& error) {
                    game.rollback(before);rollbackNative(nativeBefore);actor=previousActor;placeNativePlayer(actor);
                    scriptFault=true;game.diagnostic=error.what();notice="camera error; save in pause menu";noticeDoor=-1;
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Original camera change retained: %s",error.what());return {};
                }
            }
        }
        const Vec2 position{actor.position[0],actor.position[2]};
        if (failedPortal>=0&&!insideDoorRegion(scene.doors[failedPortal].portal,scene.triggerGrid,position)) {
            failedPortal=-1; noticeDoor=-1; notice.clear();
        }
        if (useEdge&&!canonical) use();
        const bool sourcePortalCandidate=originalRuntime&&(screenQueuedPortal||std::any_of(scene.doors.begin(),scene.doors.end(),[&](const auto& door){
            return scene.enabledPortals.at(door.portalIndex)&&insideDoorRegion(door.portal,scene.triggerGrid,position);
        }));
        if(sourcePortalCandidate) {
            const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();
            try {
                screenPortalWorker();
                if(screenQueuedPortal&&screenQueuedPortal->sourceSceneId==episode_portal_detail::sceneId(scene.id)) {
                    const auto request=*screenQueuedPortal;screenQueuedPortal.reset();
                    int index=-1;for(size_t i=0;i<scene.doors.size();++i)if(scene.doors[i].portalIndex==request.portalIndex)index=int(i);
                    if(index<0)throw jpc::Error("Queued source portal has no runtime entry");
                    if(index!=failedPortal)switchScene({request.sourceSceneId,request.destinationId,request.camera,int32_t(request.marker),request.rotationXYZ},graphics,index);
                }
            } catch(const std::exception& error) {
                game.rollback(before);rollbackNative(nativeBefore);scriptFault=true;game.diagnostic=error.what();notice="could not use passage";
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Original portal retained its previous state: %s",error.what());
            }
        } else if(!originalRuntime)for (size_t i=0;i<gates.size();++i) if (gates[i].enabled&&int(i)!=failedPortal&&
            (scene.episode||!scene.doors[i].interaction.points.empty())&&insideDoorRegion(scene.doors[i].portal,scene.triggerGrid,position)) {
            transfer(i,graphics);break;
        }
        if(originalRuntime&&dt>0)sampleSourceScreen();
        return moved;
    }
    std::string hud(bool russian=false) const {
        russian|=originalRuntime&&sourceTexts().localized();
        const auto t=[&](std::string_view value){return edui::text(value,russian);};
        const auto statusText=[&](const std::string& value){return russian?t(value).substr(0,34):value;};
        if(authoredUiComplete)return sourceMenuOwnsInput()||notice.empty()?std::string{}:statusText(notice);
        if(sourceKeyboardMenuActive())return t("controls");
        if(sourceMenuOwnsInput())return sourceMenuText(russian,authoredMainMenuVisible())+(notice.empty()?std::string{}:"\n"+statusText(notice));
        if(scene.menuContext)return statusText(notice);
        const int index=nearestDoor(false);
        std::string status="move: wasd/stick";
        if(noticeDoor==-1&&!notice.empty())status=notice;
        else if (pending>=0) status="opening...";
        else if (index>=0) {
            if (noticeDoor==index&&!notice.empty()) status=notice;
            else if (scene.doors[index].interaction.points.empty()) {
                const auto& destination=scene.doors[index].destination;
                const bool available=std::find(areas.begin(),areas.end(),destination)!=areas.end()||
                    (originalRuntime&&fs::is_regular_file((levelBPackage?root.parent_path():root/"levelB")/destination/"scene.txt"));
                status=scene.episode&&available?(gates[index].enabled?"walk through the passage":"passage closed") : "area not included yet";
            }
            else if (gates[index].enabled) status="walk through the door";
            else if (!facingDoor(scene.doors[index],{actor.position[0],actor.position[2]},actor.yaw)) status="face the door";
            else if (guard) status="wait...";
            else status="use: e / enter / a";
        }
        return statusText(status)+'\n'+t("e/enter/a: use  shift/rt: run");
    }
};

#include "package_stage.inl"

SDL_Gamepad* openFirstGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* gamepad = nullptr;
    for (int i = 0; ids && i < count && !gamepad; ++i) gamepad = SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
    return gamepad;
}
SDL_JoystickID attachVirtualGamepad() {
    SDL_VirtualJoystickDesc descriptor{}; SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD; descriptor.name = "Evil Dead input test";
    descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT; descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&descriptor);
    if (!id) throw std::runtime_error(std::string("Attach virtual gamepad: ") + SDL_GetError());
    SDL_UpdateGamepads();
    return id;
}
Vec2 gamepadStick(SDL_Gamepad* gamepad) {
    return radialStick(SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX)/32768.0f,
                       SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTY)/32768.0f);
}
bool gamepadRun(SDL_Gamepad* gamepad) {
    return SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>16384;
}
bool keyboardUseEvent(const SDL_Event& event) {
    return event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&
        (event.key.key==SDLK_E||event.key.key==SDLK_RETURN||event.key.key==SDLK_KP_ENTER);
}
void testGamepad() {
    checked(SDL_Init(SDL_INIT_GAMEPAD), "Initialize gamepad test");
    const SDL_JoystickID id = attachVirtualGamepad();
    auto* gamepad = checked(SDL_OpenGamepad(id), "Open virtual gamepad");
    auto* joystick = SDL_GetGamepadJoystick(gamepad);
    checked(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 16384), "Set virtual axis");
    checked(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), "Set virtual button");
    SDL_UpdateGamepads();
    const Vec2 half = radialStick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32768.0f,
                                  SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32768.0f);
    if (half.x < 0.38f || half.x > 0.40f || !SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH))
        throw std::runtime_error("Virtual gamepad axis/button mapping failed");
    checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,SDL_JOYSTICK_AXIS_MIN),"Release virtual run trigger");
    SDL_UpdateGamepads();
    if (gamepadRun(gamepad)) throw std::runtime_error("Interact button incorrectly starts running");
    bool interactHeld=false;
    if (!pressedEdge(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH),interactHeld) ||
        pressedEdge(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH),interactHeld))
        throw std::runtime_error("Virtual interact button is not edge triggered");
    checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,SDL_JOYSTICK_AXIS_MAX),"Press virtual run trigger");
    SDL_UpdateGamepads();
    if (!gamepadRun(gamepad)) throw std::runtime_error("Virtual run trigger failed");
    checked(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 3000), "Set deadzone axis");
    SDL_UpdateGamepads();
    if (length(radialStick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32768.0f, 0)) != 0)
        throw std::runtime_error("Virtual gamepad deadzone failed");
    SDL_CloseGamepad(gamepad);
    checked(SDL_DetachVirtualJoystick(id), "Detach virtual gamepad"); SDL_UpdateGamepads();
    int remainingCount = 0;
    SDL_JoystickID* remaining = SDL_GetGamepads(&remainingCount);
    bool stillConnected = false;
    for (int i = 0; remaining && i < remainingCount; ++i) stillConnected |= remaining[i] == id;
    SDL_free(remaining);
    if (stillConnected) throw std::runtime_error("Virtual gamepad detach failed");
    SDL_Log("SDL virtual gamepad: analog axis, action button, deadzone and disconnect passed");
    SDL_Quit();
}
struct Waypoint { Vec2 position; int camera = -1; };
std::vector<Waypoint> loadPath(const fs::path& file, const Scene& scene) {
    std::ifstream input(file); std::string magic; unsigned count=0;
    input>>magic>>count;
    if (!input || magic!="EDPATH1" || !count || count>10000) throw std::runtime_error("Invalid walkthrough path header");
    std::vector<Waypoint> path;
    for (unsigned i=0;i<count;++i) {
        Waypoint waypoint; input>>waypoint.position.x>>waypoint.position.z>>waypoint.camera;
        if (!input || !std::isfinite(waypoint.position.x) || !std::isfinite(waypoint.position.z) ||
            waypoint.camera < -1 || waypoint.camera>=int(scene.cameras.size()) ||
            !clearGround(scene.ground,waypoint.position,scene.radius))
            throw std::runtime_error("Invalid or blocked walkthrough waypoint "+std::to_string(i));
        path.push_back(waypoint);
    }
    return path;
}
void traceHeader(std::ostream& output) {
    output<<"tick,time,x,y,z,camera_index,stickX,stickZ,desiredDX,desiredDZ,requestedDistance,movedDistance\n"<<std::setprecision(9);
}
void traceStep(std::ostream& output, unsigned tick, double time, const Actor& actor, Vec2 stick, ActorStep step) {
    output<<tick<<','<<time<<','<<actor.position[0]<<','<<actor.position[1]<<','<<actor.position[2]<<','<<actor.camera<<','
          <<stick.x<<','<<stick.z<<','<<step.desiredDelta.x<<','<<step.desiredDelta.z<<','<<step.requested<<','<<step.moved<<'\n';
}
void testScene(const Scene& scene, const fs::path& assets, unsigned initialCamera, std::ostream* trace,
               bool draw, int width, int height, const fs::path& capture) {
    const auto path=loadPath(assets/"walkthrough.txt",scene);
    unsigned probes=0;
    for (const auto& polygon:scene.ground) if (!polygon.keepInside) for (size_t edge=0;edge<polygon.points.size();++edge) {
        const Vec2 midpoint=(polygon.points[edge]+polygon.points[(edge+1)%polygon.points.size()])*0.5f;
        const Vec2 normal=nearestBoundary(polygon,midpoint).outward;
        const Vec2 start=midpoint+normal*(scene.radius+0.05f);
        if (!clearGround(scene.ground,start,scene.radius)) continue;
        const Vec2 delta=normal*(-2*scene.radius-0.3f);
        const Vec2 end=moveGround(scene.ground,start,delta,scene.radius);
        if (!clearGround(scene.ground,end,scene.radius) || length(end-start)>length(delta)-0.01f)
            throw std::runtime_error("Original wall did not block swept motion");
        ++probes;
    }
    if (!probes) throw std::runtime_error("No accessible original walls were tested");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize scene gamepad test");
    std::unique_ptr<Graphics> graphics;
    if (draw) {
        graphics=std::make_unique<Graphics>();
        graphics->initialize(scene,assets,width,height);
    }
    const SDL_JoystickID id=attachVirtualGamepad();
    auto* gamepad=checked(SDL_OpenGamepad(id),"Open scene virtual gamepad");
    auto* joystick=SDL_GetGamepadJoystick(gamepad);
    Actor actor=resetActor(scene,initialCamera);
    unsigned stressBlocked=0;
    uint32_t random=0xed01c0deu;
    Vec2 stressRaw{};
    const float stressDt[]={1.0f/120,1.0f/60,1.0f/30,0.05f};
    for (unsigned frame=0;frame<4096;++frame) {
        if (frame%64==0) {
            random=random*1664525u+1013904223u;
            const float angle=float(random&0xffffu)*(6.28318530718f/65536);
            random=random*1664525u+1013904223u;
            const float magnitude=0.3f+0.7f*float(random&0xffffu)/65535;
            stressRaw={std::cos(angle)*magnitude,std::sin(angle)*magnitude};
        }
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,Sint16(stressRaw.x*32767)),"Set stress stick X");
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,Sint16(stressRaw.z*32767)),"Set stress stick Y");
        checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
            (frame/64)%2!=0?SDL_JOYSTICK_AXIS_MAX:SDL_JOYSTICK_AXIS_MIN),"Set stress run trigger");
        SDL_UpdateGamepads();
        const Vec2 input=gamepadStick(gamepad);
        const bool run=gamepadRun(gamepad);
        const float dt=stressDt[frame%4];
        Movement expectedMovement=actor.movement;
        const Camera& camera=scene.cameras[actor.camera];
        const Vec2 desired=expectedMovement.world(input,camera.right,camera.forward)*(scene.speed*(run?2.0f:1.0f)*dt);
        try {
            const ActorStep step=stepActor(scene,actor,input,run,dt);
            if (!clearGround(scene.ground,{actor.position[0],actor.position[2]},scene.radius) || step.moved>step.requested+ContactSkin)
                throw std::runtime_error("Invalid collision clearance or movement length");
            if (step.moved+0.00001f<step.requested) ++stressBlocked;
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("Scene stress tick "+std::to_string(frame)+" at ("+
                std::to_string(actor.position[0])+","+std::to_string(actor.position[2])+
                "), delta ("+std::to_string(desired.x)+","+std::to_string(desired.z)+
                "): "+error.what());
        }
    }
    if (stressBlocked<20) throw std::runtime_error("Scene stress did not exercise enough blocked movement");
    checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0),"Release stress stick X");
    checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,0),"Release stress stick Y");
    checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,SDL_JOYSTICK_AXIS_MIN),"Release stress run trigger");
    SDL_UpdateGamepads();
    actor=resetActor(scene,initialCamera);
    constexpr float dt=1.0f/120;
    unsigned tick=0, cuts=0, collided=0;
    bool stopped=false;
    if (trace) traceHeader(*trace);
    for (size_t waypoint=0;waypoint<path.size();++waypoint) {
        bool arrived=false;
        for (unsigned frame=0;frame<1200;++frame) {
            if (draw) {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                    if (event.type==SDL_EVENT_QUIT || (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE)) stopped=true;
                if (stopped) break;
            }
            const Vec2 difference=path[waypoint].position-Vec2{actor.position[0],actor.position[2]};
            const float distance=length(difference);
            if (distance<0.025f) {
                if (path[waypoint].camera>=0 && actor.camera!=unsigned(path[waypoint].camera))
                    throw std::runtime_error("Wrong camera at walkthrough waypoint "+std::to_string(waypoint));
                checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,0),"Release scene stick X");
                checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,0),"Release scene stick Y");
                SDL_UpdateGamepads();
                const Vec2 released=gamepadStick(gamepad);
                const ActorStep releaseStep=stepActor(scene,actor,released,false,dt);
                if (trace) traceStep(*trace,tick,double(tick+1)*dt,actor,released,releaseStep);
                ++tick;
                arrived=true; break;
            }
            const Camera& camera=scene.cameras[actor.camera];
            const Vec2 right=actor.movement.held?actor.movement.right:camera.right;
            const Vec2 forward=actor.movement.held?actor.movement.forward:camera.forward;
            const float determinant=right.x*forward.z-right.z*forward.x;
            if (std::abs(determinant)<0.01f) throw std::runtime_error("Camera ground basis is singular");
            Vec2 desired{(difference.x*forward.z-difference.z*forward.x)/determinant,
                         (right.x*difference.z-right.z*difference.x)/determinant};
            desired=desired*(1/length(desired));
            const float rawMagnitude=0.18f+0.82f*std::min(1.0f,distance/(scene.speed*dt));
            const Vec2 raw{desired.x*rawMagnitude,-desired.z*rawMagnitude};
            checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,Sint16(std::clamp(raw.x,-1.0f,1.0f)*32767)),"Set scene stick X");
            checked(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,Sint16(std::clamp(raw.z,-1.0f,1.0f)*32767)),"Set scene stick Y");
            SDL_UpdateGamepads();
            const unsigned oldCamera=actor.camera;
            const Vec2 input=gamepadStick(gamepad);
            const ActorStep result=stepActor(scene,actor,input,false,dt);
            if (actor.camera!=oldCamera) {
                if (!actor.movement.held) throw std::runtime_error("Camera cut cleared held movement");
                ++cuts;
            }
            if (graphics && (tick%4==0 || actor.camera!=oldCamera)) {
                graphics->render(scene,actor.camera,actor.position,actor.yaw,actor.animationTime,actor.walking);
                if (actor.camera!=oldCamera && cuts<=2 && !capture.empty()) {
                    fs::path capturePath=capture;
                    if (cuts==2) capturePath=capture.parent_path()/(capture.stem().string()+"-cut-2"+capture.extension().string());
                    graphics->screenshot(capturePath);
                }
            }
            if (result.moved+0.00001f<result.requested) ++collided;
            if (trace) traceStep(*trace,tick,double(tick+1)*dt,actor,input,result);
            ++tick;
        }
        if (stopped) break;
        if (!arrived) throw std::runtime_error("Walkthrough could not reach waypoint "+std::to_string(waypoint)+
            " from ("+std::to_string(actor.position[0])+","+std::to_string(actor.position[2])+
            ") to ("+std::to_string(path[waypoint].position.x)+","+std::to_string(path[waypoint].position.z)+")");
    }
    if (!cuts && !stopped) throw std::runtime_error("Walkthrough did not exercise an automatic camera cut");
    SDL_CloseGamepad(gamepad); checked(SDL_DetachVirtualJoystick(id),"Detach scene test gamepad");
    graphics.reset(); SDL_Quit();
    if (stopped) { std::cout<<"Walkthrough stopped by user\n"; return; }
    std::cout<<"Scene self-test passed: "<<path.size()<<" waypoints, "<<tick<<" steps, "<<cuts
             <<" camera cuts, "<<collided<<" collision steps, "<<probes<<" original wall sweeps, "
             <<stressBlocked<<" blocked SDL stress steps\n";
}

void presentMovieHeadless(World& world,uint32_t identity,int flags) {
    world.rumbleHost.send({});
    if(flags&4){world.sourceScreen.fadeOut();world.waitForScreenFade();}
    MoviePlayer movie(world.root,identity);movie.setStereo(world.sourceGui.stereo!=0);movie.start(false);
    const double interval=movie.duration()/movie.frameCount();
    for(unsigned frame=0;frame<=movie.frameCount()&&!movie.done();++frame)movie.advance(interval+1e-9);
    if(!movie.done())throw std::runtime_error("Original movie did not finish in bounded frame replay");
    world.advanceIdleTime(movie.duration());
}

#include "package_save.inl"
#include "cart_process.inl"

struct QuitRequested {};
void presentMovie(World& world,Graphics& graphics,SDL_Gamepad*& gamepad,uint32_t identity,int flags,int width,int height) {
    world.rumbleHost.send({});edrumble::silence(gamepad);
    if(!graphics.window)graphics.initialize(world.nativeScene(),world.root/world.nativeScene().id,width,height);
    graphics.loadFont(world.root);graphics.loadInventory(world.root);
    if(world.actors)graphics.loadActorTextures(world.root,*world.actors);
    if(flags&4){world.sourceScreen.fadeOut();world.waitForScreenFade(&graphics);}
    MoviePlayer movie(world.root,identity);
    struct RestoreVolume {
        World& world;bool loopsMuted,streamMuted;float doorGain;
        ~RestoreVolume(){world.audioBank.muteLoops(loopsMuted);if(world.streams){world.streams->mute(streamMuted);world.streams->commit();}
            if(world.audio.stream)SDL_SetAudioStreamGain(world.audio.stream,doorGain);}
    } restore{world,world.audioBank.loopsMuted(),world.streams?world.streams->muted():false,world.audio.stream?SDL_GetAudioStreamGain(world.audio.stream):1.0f};
    // Let already-started speech/SFX tails finish while the movie replaces scene ambience.
    world.audioBank.muteLoops(true);world.audioBank.openDevice();
    if(world.streams){world.streams->mute(true);world.streams->openDevice();world.streams->commit();}
    if(world.audio.stream)SDL_SetAudioStreamGain(world.audio.stream,0);
    movie.setStereo(world.sourceGui.stereo!=0);movie.start(true);
    bool focused=true;Uint64 previous=SDL_GetTicksNS();
    SDL_SetWindowTitle(graphics.window,"Evil Dead | cutscene | Esc / Space / B: skip");
    while(!movie.done()) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_EVENT_QUIT)throw QuitRequested{};
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){focused=false;movie.pause(true);}
            if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED){focused=true;movie.pause(false);}
            if(event.type==SDL_EVENT_GAMEPAD_REMOVED&&gamepad&&event.gdevice.which==SDL_GetGamepadID(gamepad)) {
                edrumble::silence(gamepad);SDL_CloseGamepad(gamepad);gamepad=openFirstGamepad();
            }
            if(event.type==SDL_EVENT_GAMEPAD_ADDED&&!gamepad)gamepad=SDL_OpenGamepad(event.gdevice.which);
            if(focused&&event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&(event.key.key==SDLK_ESCAPE||event.key.key==SDLK_SPACE))movie.skip();
            if(focused&&gamepad&&event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&event.gbutton.which==SDL_GetGamepadID(gamepad)&&event.gbutton.button==SDL_GAMEPAD_BUTTON_EAST)movie.skip();
        }
        const auto now=SDL_GetTicksNS();const double dt=double(now-previous)/1e9;previous=now;
        world.advanceIdleTime(dt);movie.advance(dt);
        graphics.renderMovie(movie);SDL_Delay(1);
    }
}

#include "door_flow_tests.inl"
#include "script_flow_tests.inl"
#include "world_save_tests.inl"
#include "movie_flow_tests.inl"
#include "episode_flow_tests.inl"
#include "title_walk_world_tests.inl"
#include "debug_menu_world_tests.inl"
#include "ah02_walk_world_tests.inl"
#include "ah02_handle_walk_world_tests.inl"
#include "ah05_walk_world_tests.inl"
#include "ah05_return_walk_world_tests.inl"
#include "crowbar_walk_world_tests.inl"
#include "ah05_platform_walk_world_tests.inl"
#include "ah12_return_walk_world_tests.inl"
#include "ah06_matriarch_walk_world_tests.inl"
#include "tree_walk_world_tests.inl"
#include "story_world_tests.inl"
#include "story_tree_world_tests.inl"
#include "story_pages_world_tests.inl"
#include "story_troop_world_tests.inl"
#include "story_last_page_world_tests.inl"
#include "story_offering_world_tests.inl"
#include "story_ending_world_tests.inl"
#include "levelb_story_world_tests.inl"
#include "levelb_merchant_world_tests.inl"
#include "levelb_bomb_world_tests.inl"
#include "levelb_temple_world_tests.inl"
#include "levelb_wizard_world_tests.inl"
#include "levelb_pump_world_tests.inl"
#include "joined_story_world_tests.inl"
#include "bridge_walk_world_tests.inl"
#include "earned_bridge_walk_world_tests.inl"
#include "earned_bm02_walk_world_tests.inl"
#include "earned_merchant_walk_world_tests.inl"
#include "earned_bomb_walk_world_tests.inl"
#include "earned_temple_key_walk_world_tests.inl"
#include "earned_temple_statues_walk_world_tests.inl"
#include "earned_bm01_input_walk_world_tests.inl"
#include "audio_world_tests.inl"
#include "object_persistence_world_tests.inl"
#include "source_scene_flow_tests.inl"
#include "levelb_world_tests.inl"
#include "screen_flow_tests.inl"
#include "package_flow_tests.inl"
#include "package_gpu_tests.inl"
#include "menu_context_tests.inl"
#include "menu_world_tests.inl"
#include "scene_vfx_world_tests.inl"
#include "source_pose_tests.inl"
#include "annie_world_tests.inl"
#include "cart_world_tests.inl"
#include "death_world_tests.inl"
#include "cart_exit_tests.inl"
#include "locale_save_world_tests.inl"
#include "document_world_tests.inl"
#include "game_over_flow_tests.inl"
#include "inventory_frame_tests.inl"
#include "armed_locomotion_world_tests.inl"
#include "chainsaw_controls_world_tests.inl"
#include "source_input_controls_world_tests.inl"
#include "pickup_controls_world_tests.inl"
#include "native_motion_world_tests.inl"
#include "native_combat_controls_world_tests.inl"
#include "native_locomotion_world_tests.inl"
#include "source_rumble_world_tests.inl"
#include "dreamcast_runtime_tests.inl"

int main(int argc, char** argv) {
    try {
        fs::path assets = fs::u8path(SDL_GetBasePath()) / ".." / "assets", screenshot, tracePath;
        bool smoke = false, gamepadTest = false, sceneTest = false, walkthrough = false,doorTest=false,cameraSet=false,scriptTest=false,saveTest=false,movieTest=false,episodeTest=false,sourceScenesTest=false,levelBWorldTest=false,screenFlowTest=false;
        int windowWidth = 0, windowHeight = 0, firstCamera = 1;
        bool nativeCombatControlsTest=false;
        bool nativeLocomotionTest=false;
        bool sourceRumbleTest=false;
        bool pickupControlsTest=false;
        bool ah02WalkWorldTest=false,ah02HandleWalkWorldTest=false,ah05WalkWorldTest=false,ah05ReturnWalkWorldTest=false,crowbarWalkWorldTest=false,ah05PlatformWalkWorldTest=false,ah10KeydoorWalkWorldTest=false,ah10PumpWalkWorldTest=false,ah12AmuletWalkWorldTest=false,ah12ReturnWalkWorldTest=false,earnedBridgeWalkWorldTest=false,earnedBM02WalkWorldTest=false,earnedMerchantWalkWorldTest=false,earnedBombWalkWorldTest=false,earnedTempleKeyWalkWorldTest=false,earnedTempleStatuesWalkWorldTest=false,levelBFoundryWorldTest=false,levelBEndingWorldTest=false;
        bool ah06MatriarchWalkWorldTest=false;
        bool treeWalkWorldTest=false;
        bool titleEpisodeWalkWorldTest=false;
        bool debugMenuWorldTest=false,dreamcastRuntimeTest=false;
        bool earnedBM01TempleWalkWorldTest=false;
        bool packageFlowTest=false,packageGpuTest=false,menuContextTest=false,menuWorldTest=false,sceneVfxWorldTest=false,sourcePoseTest=false,annieWorldTest=false,cartWorldTest=false,deathWorldTest=false,cartExitTest=false,documentsTest=false,gameOverFlowTest=false,inventoryFramesTest=false,storyWorldTest=false,treeStoryWorldTest=false,pagesStoryWorldTest=false,troopStoryWorldTest=false,lastPageStoryWorldTest=false,offeringStoryWorldTest=false,earnedEndingWorldTest=false,joinedStoryWorldTest=false,bridgeWalkWorldTest=false,levelBStoryWorldTest=false,levelBMerchantWorldTest=false,levelBBombWorldTest=false,levelBTempleWorldTest=false,levelBWizardWorldTest=false,levelBPumpWorldTest=false,nativeMotionTest=false,audioWorldTest=false,objectPersistenceWorldTest=false;
        auto integer = [](const char* text) {
            size_t consumed = 0;
            const int value = std::stoi(text, &consumed);
            if (consumed != std::strlen(text)) throw std::runtime_error("Expected an integer argument");
            return value;
        };
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--smoke") smoke = true;
            else if (argument == "--self-test-gamepad") gamepadTest = true;
            else if (argument == "--self-test-scene") sceneTest = true;
            else if (argument == "--self-test-doors") doorTest=true;
            else if(argument=="--self-test-scripts")scriptTest=true;
            else if(argument=="--self-test-save")saveTest=true;
            else if(argument=="--self-test-movies")movieTest=true;
            else if(argument=="--self-test-episode")episodeTest=true;
            else if(argument=="--self-test-ah02-walk-world")ah02WalkWorldTest=true;
            else if(argument=="--self-test-ah02-handle-walk-world")ah02HandleWalkWorldTest=true;
            else if(argument=="--self-test-ah05-walk-world")ah05WalkWorldTest=true;
            else if(argument=="--self-test-ah05-return-walk-world")ah05ReturnWalkWorldTest=true;
            else if(argument=="--self-test-crowbar-walk-world")crowbarWalkWorldTest=true;
            else if(argument=="--self-test-ah05-platform-walk-world")ah05PlatformWalkWorldTest=true;
            else if(argument=="--self-test-ah10-keydoor-walk-world")ah10KeydoorWalkWorldTest=true;
            else if(argument=="--self-test-ah10-pump-walk-world")ah10PumpWalkWorldTest=true;
            else if(argument=="--self-test-ah12-amulet-walk-world")ah12AmuletWalkWorldTest=true;
            else if(argument=="--self-test-ah12-return-walk-world")ah12ReturnWalkWorldTest=true;
            else if(argument=="--self-test-ah06-matriarch-walk-world")ah06MatriarchWalkWorldTest=true;
            else if(argument=="--self-test-tree-walk-world")treeWalkWorldTest=true;
            else if(argument=="--self-test-title-episode-walk-world")titleEpisodeWalkWorldTest=true;
            else if(argument=="--self-test-debug-menu-world")debugMenuWorldTest=true;
            else if(argument=="--self-test-dreamcast-runtime")dreamcastRuntimeTest=true;
            else if(argument=="--self-test-source-scenes")sourceScenesTest=true;
            else if(argument=="--self-test-levelb-world")levelBWorldTest=true;
            else if(argument=="--self-test-screen-flow")screenFlowTest=true;
            else if(argument=="--self-test-package-flow")packageFlowTest=true;
            else if(argument=="--self-test-package-gpu")packageGpuTest=true;
            else if(argument=="--self-test-menu-context")menuContextTest=true;
            else if(argument=="--self-test-menu-world")menuWorldTest=true;
            else if(argument=="--self-test-scene-vfx-world")sceneVfxWorldTest=true;
            else if(argument=="--self-test-source-pose")sourcePoseTest=true;
            else if(argument=="--self-test-annie-world")annieWorldTest=true;
            else if(argument=="--self-test-cart-world")cartWorldTest=true;
            else if(argument=="--self-test-death-world")deathWorldTest=true;
            else if(argument=="--self-test-cart-exit")cartExitTest=true;
            else if(argument=="--self-test-documents")documentsTest=true;
            else if(argument=="--self-test-game-over-flow")gameOverFlowTest=true;
            else if(argument=="--self-test-inventory-frames")inventoryFramesTest=true;
            else if(argument=="--self-test-story-world")storyWorldTest=true;
            else if(argument=="--self-test-tree-story-world")treeStoryWorldTest=true;
            else if(argument=="--self-test-pages-story-world")pagesStoryWorldTest=true;
            else if(argument=="--self-test-troop-story-world")troopStoryWorldTest=true;
            else if(argument=="--self-test-last-page-story-world")lastPageStoryWorldTest=true;
            else if(argument=="--self-test-offering-story-world")offeringStoryWorldTest=true;
            else if(argument=="--self-test-earned-ending-world")earnedEndingWorldTest=true;
            else if(argument=="--self-test-joined-story-world")joinedStoryWorldTest=true;
            else if(argument=="--self-test-bridge-walk-world")bridgeWalkWorldTest=true;
            else if(argument=="--self-test-earned-bridge-walk-world")earnedBridgeWalkWorldTest=true;
            else if(argument=="--self-test-earned-bm02-walk-world")earnedBM02WalkWorldTest=true;
            else if(argument=="--self-test-earned-merchant-walk-world")earnedMerchantWalkWorldTest=true;
            else if(argument=="--self-test-earned-bomb-walk-world")earnedBombWalkWorldTest=true;
            else if(argument=="--self-test-earned-temple-key-walk-world")earnedTempleKeyWalkWorldTest=true;
            else if(argument=="--self-test-earned-temple-statues-walk-world")earnedTempleStatuesWalkWorldTest=true;
            else if(argument=="--self-test-earned-bm01-temple-walk-world")earnedBM01TempleWalkWorldTest=true;
            else if(argument=="--self-test-audio-world")audioWorldTest=true;
            else if(argument=="--self-test-object-persistence")objectPersistenceWorldTest=true;
            else if(argument=="--self-test-levelb-story-world")levelBStoryWorldTest=true;
            else if(argument=="--self-test-levelb-merchant-world")levelBMerchantWorldTest=true;
            else if(argument=="--self-test-levelb-bomb-world")levelBBombWorldTest=true;
            else if(argument=="--self-test-levelb-temple-world")levelBTempleWorldTest=true;
            else if(argument=="--self-test-levelb-wizard-world")levelBWizardWorldTest=true;
            else if(argument=="--self-test-levelb-pump-world")levelBPumpWorldTest=true;
            else if(argument=="--self-test-levelb-foundry-world")levelBFoundryWorldTest=true;
            else if(argument=="--self-test-levelb-ending-world")levelBEndingWorldTest=true;
            else if(argument=="--self-test-native-motion")nativeMotionTest=true;
            else if(argument=="--self-test-native-combat-controls-world")nativeCombatControlsTest=true;
            else if(argument=="--self-test-pickup-controls-world")pickupControlsTest=true;
            else if(argument=="--self-test-native-locomotion-world")nativeLocomotionTest=true;
            else if(argument=="--self-test-source-rumble-world")sourceRumbleTest=true;
            else if (argument == "--walkthrough") walkthrough = true;
            else if (argument == "--size" && i + 2 < argc) {
                windowWidth = integer(argv[++i]); windowHeight = integer(argv[++i]);
                if (windowWidth < 160 || windowHeight < 120 || windowWidth > 16384 || windowHeight > 16384)
                    throw std::runtime_error("Window size must be between 160x120 and 16384x16384");
            }
            else if (argument == "--camera" && i + 1 < argc) { firstCamera = integer(argv[++i]); cameraSet=true; }
            else if ((argument == "--assets" || argument == "--screenshot" || argument=="--trace") && i + 1 < argc) {
                if (argument == "--assets") assets = fs::u8path(argv[++i]);
                else if (argument=="--trace") tracePath=fs::u8path(argv[++i]);
                else screenshot = fs::u8path(argv[++i]);
            } else throw std::runtime_error("Usage: evil-dead-scene [--assets directory] [--size width height] [--camera 1..N] [--smoke] [--screenshot file.bmp] [--self-test-gamepad] [--self-test-scene] [--self-test-doors] [--walkthrough] [--trace file.csv]");
        }
        if (gamepadTest) { testGamepad(); return 0; }
        std::ofstream trace;
        if (!tracePath.empty()) {
            trace.open(tracePath);
            if (!trace) throw std::runtime_error("Cannot create trajectory trace");
        }
        if (doorTest) {
            testDoors(assets,walkthrough,windowWidth,windowHeight,screenshot,trace?&trace:nullptr);
            return 0;
        }
        if(scriptTest){testScriptFlow(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(saveTest){testWorldSave(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(movieTest){testMovies(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(episodeTest){testEpisode(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(ah02WalkWorldTest){testAH02WalkWorld(assets);return 0;}
        if(ah02HandleWalkWorldTest){testAH02HandleWalkWorld(assets);return 0;}
        if(ah05WalkWorldTest){testAH05WalkWorld(assets);return 0;}
        if(ah05ReturnWalkWorldTest){testAH05ReturnWalkWorld(assets);return 0;}
        if(crowbarWalkWorldTest){testCrowbarWalkWorld(assets);return 0;}
        if(ah05PlatformWalkWorldTest){testAH05PlatformWalkWorld(assets);return 0;}
        if(ah10KeydoorWalkWorldTest){testAH10KeydoorWalkWorld(assets);return 0;}
        if(ah10PumpWalkWorldTest){testAH10PumpWalkWorld(assets);return 0;}
        if(ah12AmuletWalkWorldTest){testAH12AmuletWalkWorld(assets);return 0;}
        if(ah12ReturnWalkWorldTest){testAH12ReturnWalkWorld(assets);return 0;}
        if(ah06MatriarchWalkWorldTest){testAH06MatriarchWalkWorld(assets);return 0;}
        if(treeWalkWorldTest){testTreeWalkWorld(assets);return 0;}
        if(titleEpisodeWalkWorldTest){testTitleEpisodeWalkWorld(assets);return 0;}
        if(debugMenuWorldTest){testDebugMenuWorld(assets,screenshot);return 0;}
        if(dreamcastRuntimeTest){testDreamcastRuntime(assets);return 0;}
        if(sourceScenesTest){testSourceScenes(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(levelBWorldTest){testLevelBWorld(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(screenFlowTest){testScreenFlow(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(packageFlowTest){testPackageFlow(assets,assets/"levelB");return 0;}
        if(packageGpuTest){testPackageGpu(assets,assets/"levelB",windowWidth,windowHeight,screenshot);return 0;}
        if(menuContextTest){testMenuContexts(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(menuWorldTest){testMenuWorld(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(sceneVfxWorldTest){testSceneVfxWorld(assets,assets/"levelB",walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(sourcePoseTest){testSourcePose(assets);return 0;}
        if(annieWorldTest){testAnnieWorld(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(cartWorldTest){testCartWorld(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(deathWorldTest){testDeathWorld(assets);return 0;}
        if(cartExitTest){testCartExit(assets);return 0;}
        if(documentsTest){testDocumentsWorld(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(gameOverFlowTest){testGameOverFlow(assets,walkthrough,windowWidth,windowHeight,screenshot);return 0;}
        if(inventoryFramesTest){testInventoryFrames(assets);return 0;}
        if(storyWorldTest){testStoryWorld(assets);return 0;}
        if(treeStoryWorldTest){testTreeStoryWorld(assets);return 0;}
        if(pagesStoryWorldTest){testPagesStoryWorld(assets);return 0;}
        if(troopStoryWorldTest){testTroopStoryWorld(assets);return 0;}
        if(lastPageStoryWorldTest){testLastPageStoryWorld(assets);return 0;}
        if(offeringStoryWorldTest){testOfferingStoryWorld(assets);return 0;}
        if(earnedEndingWorldTest){testEarnedEndingWorld(assets);return 0;}
        if(joinedStoryWorldTest){testJoinedStoryWorld(assets);return 0;}
        if(bridgeWalkWorldTest){testBridgeWalkWorld(assets);return 0;}
        if(earnedBridgeWalkWorldTest){testEarnedBridgeWalkWorld(assets);return 0;}
        if(earnedBM02WalkWorldTest){testEarnedBM02WalkWorld(assets);return 0;}
        if(earnedMerchantWalkWorldTest){testEarnedMerchantWalkWorld(assets);return 0;}
        if(earnedBombWalkWorldTest){testEarnedBombWalkWorld(assets);return 0;}
        if(earnedTempleKeyWalkWorldTest){testEarnedTempleKeyWalkWorld(assets);return 0;}
        if(earnedTempleStatuesWalkWorldTest){testEarnedTempleStatuesWalkWorld(assets);return 0;}
        if(earnedBM01TempleWalkWorldTest){testEarnedBM01TempleWalkWorld(assets);return 0;}
        if(audioWorldTest){testAudioWorld(assets);return 0;}
        if(objectPersistenceWorldTest){testObjectPersistenceWorld(assets);return 0;}
        if(levelBStoryWorldTest){testLevelBStoryWorld(assets);return 0;}
        if(levelBMerchantWorldTest){testLevelBMerchantWorld(assets);return 0;}
        if(levelBBombWorldTest){testLevelBBombWorld(assets);return 0;}
        if(levelBTempleWorldTest){testLevelBTempleWorld(assets);return 0;}
        if(levelBWizardWorldTest){testLevelBWizardWorld(assets);return 0;}
        if(levelBPumpWorldTest){testLevelBPumpWorld(assets);return 0;}
        if(levelBFoundryWorldTest){testLevelBFoundryWorld(assets);return 0;}
        if(levelBEndingWorldTest){testLevelBEndingWorld(assets);return 0;}
        if(nativeMotionTest){testNativeMotionWorld(assets);return 0;}
        if(nativeCombatControlsTest){testNativeCombatControlsWorld(assets);return 0;}
        if(nativeLocomotionTest){testNativeLocomotionWorld(assets);return 0;}
        if(sourceRumbleTest){testSourceRumbleWorld(assets);return 0;}
        if(pickupControlsTest){testPickupControlsWorld(assets);return 0;}
        if (sceneTest || walkthrough) {
            const Scene scene=loadScene(assets);
            if (firstCamera < 1 || firstCamera > int(scene.cameras.size())) throw std::runtime_error("Camera number out of range");
            testScene(scene,assets,unsigned(firstCamera-1),trace?&trace:nullptr,walkthrough,windowWidth,windowHeight,screenshot);
            return 0;
        }
        checked(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "Initialize SDL");
        {
            Graphics graphics;
            SDL_Gamepad* gamepad=openFirstGamepad();
            edrumble::Feedback feedback;
            struct FeedbackCleanup {edrumble::Feedback& feedback;SDL_Gamepad*& pad;~FeedbackCleanup(){feedback.reset(pad);}} feedbackCleanup{feedback,gamepad};
            edrumble::Host rumbleHost;
            rumbleHost.available=[&](int port){return edrumble::available(gamepad,port);};
            rumbleHost.rumble=[&](edrumble::Command command){feedback.command(command);};
            ControlSettings controlSettings;
            ControlInput controlInput;
            ControlMenu controlMenu;
            VideoSettings videoSettings;
            SettingsMenu settingsMenu;
            bool showOriginalOptions=false,sourceOptionsSeen=false;
            char* preferences=checked(SDL_GetPrefPath("EvilDeadCommunity","HailToTheKing"),"Locate save directory");
            const fs::path preferenceDirectory=fs::u8path(preferences);SDL_free(preferences);
            try {controlSettings.load(preferenceDirectory);}
            catch(const std::exception& error){SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,"Retaining default controls: %s",error.what());}
            try {videoSettings.load(preferenceDirectory);}
            catch(const std::exception& error){SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,"Retaining default video settings: %s",error.what());}
            const bool explicitSize=windowWidth!=0||windowHeight!=0;
            if(explicitSize){videoSettings.width=videoSettings.height=0;videoSettings.fullscreen=false;}
            graphics.setPostEffects(videoSettings);
            graphics.setUiLanguage(videoSettings.language);
            bool initialDisplayApplied=false;
            const auto applySavedDisplayOnce=[&]{
                if(initialDisplayApplied||!graphics.window)return;
                try {
                    if(!explicitSize&&(videoSettings.width||videoSettings.fullscreen))
                        graphics.applyDisplayMode(videoSettings.width,videoSettings.height,videoSettings.fullscreen);
                } catch(const std::exception& error){
                    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,"Saved display mode unavailable; retaining auto/current window: %s",error.what());
                    videoSettings.width=videoSettings.height=0;
                    videoSettings.fullscreen=(SDL_GetWindowFlags(graphics.window)&SDL_WINDOW_FULLSCREEN)!=0;
                }
                graphics.setPostEffects(videoSettings);initialDisplayApplied=true;
            };
            const auto applyVideoSettings=[&](const VideoSettings& candidate){
                try {
                    graphics.setUiLanguage(candidate.language);
                    const auto commit=[&]{candidate.save(preferenceDirectory);};
                    if(candidate.width!=videoSettings.width||candidate.height!=videoSettings.height||candidate.fullscreen!=videoSettings.fullscreen||
                       candidate.fullscreen!=bool(SDL_GetWindowFlags(graphics.window)&SDL_WINDOW_FULLSCREEN))
                        graphics.applyDisplayMode(candidate.width,candidate.height,candidate.fullscreen,commit);
                    else commit();
                    videoSettings=candidate;graphics.setPostEffects(candidate);return true;
                }catch(const std::exception& error){
                    try{graphics.setUiLanguage(videoSettings.language);}catch(const std::exception& restore){SDL_LogError(SDL_LOG_CATEGORY_VIDEO,"Could not restore previous UI language: %s",restore.what());}
                    settingsMenu.setNotice("could not apply settings");
                    SDL_LogError(SDL_LOG_CATEGORY_VIDEO,"Settings retained their previous state: %s",error.what());return false;
                }
            };
            uint64_t presentedMovies=0;
            World::MoviePresenter moviePresentation=[&](World& active,uint32_t identity,int flags){
                if(smoke)presentMovieHeadless(active,identity,flags);
                else {
                    if(!graphics.window)graphics.initialize(active.nativeScene(),active.root/active.nativeScene().id,windowWidth,windowHeight);
                    applySavedDisplayOnce();
                    presentMovie(active,graphics,gamepad,identity,flags,windowWidth,windowHeight);
                }
                controlInput.reset();++presentedMovies;
            };
            const bool originalAssets=fs::exists(assets/"episode.json");
            auto ownedWorld=std::make_unique<World>(assets,trace?&trace:nullptr,moviePresentation,originalAssets,nullptr,originalAssets,rumbleHost);
            auto& startup=*ownedWorld;
            startup.setUiLanguage(videoSettings.language);
            edutility::Presenter utilityUi(startup.root);
            fs::path saveDirectory=savefile::detail::directory(preferenceDirectory);
            if(startup.game.profile.dreamcast){saveDirectory/="dreamcast";fs::create_directory(saveDirectory);saveDirectory=savefile::detail::directory(saveDirectory);}
            startup.configureSourceCart(saveDirectory);
            if (cameraSet) {
                if (firstCamera<1||firstCamera>int(startup.scene.cameras.size())) throw std::runtime_error("Camera number out of range");
                startup.actor.camera=unsigned(firstCamera-1);
            }
            if(!graphics.window)graphics.initialize(startup.scene,startup.directory(),windowWidth,windowHeight);
            applySavedDisplayOnce();
            graphics.loadFont(assets);graphics.loadInventory(assets);if(startup.actors)graphics.loadActorTextures(assets,*startup.actors);
            startup.audio.openDevice();startup.audioBank.openDevice();if(startup.streams)startup.streams->openDevice();
            bool confirmHeld=gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH);
            bool cancelHeld=gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_EAST);
            bool pauseHeld=gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_BACK);
            auto seedMenuButtons=[&] {
                confirmHeld=gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH);
                cancelHeld=gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_EAST);
                pauseHeld=gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_BACK);
            };
            uint64_t observedMovies=presentedMovies;
            uint64_t observedScreenRevision=startup.screenPresentationRevision;
            InventoryNavigator inventoryNavigation;
            PauseMenu pauseMenu;
            DebugMenu debugMenu;
            bool debugTextInputActive=false;
            bool sourceControlsPopup=false;
            unsigned sourceMenuDirections=0;
            bool seedSourceMenuDirections=true;
            const auto dispatchSourceMenu=[&](World& world,const std::vector<int>& events){
                const auto before=world.game.checkpoint();const auto nativeBefore=world.checkpointNative();world.beginNativeStage();
                try{world.sourceMenuInput(events);world.commitNativeStage();return true;}
                catch(const std::exception& error){world.game.rollback(before);world.rollbackNative(nativeBefore);
                    world.notice="could not use menu action";
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Menu retained its previous state: %s",error.what());return false;}
            };
            unsigned frames = 0;
            bool running = true, focused = true;
            Uint64 previous = SDL_GetTicksNS();
            edclock::FrameGate frameGate{uint32_t(SDL_GetTicks())};
            while (running) {
                auto& world=*ownedWorld;world.debugSettings=&debugMenu.settings;
                if(world.originalRuntime&&!frameGate.offer(uint32_t(SDL_GetTicks()))){SDL_Delay(1);continue;}
                const bool optionsActive=world.sourceOptionsMenuActive();
                if(optionsActive&&!sourceOptionsSeen){
                    settingsMenu.open(videoSettings,graphics.displayResolutions());showOriginalOptions=false;
                    controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                }else if(!optionsActive){settingsMenu.close();showOriginalOptions=false;}
                sourceOptionsSeen=optionsActive;
                bool menuConfirm=false,menuBack=false,menuCombine=false,menuControls=false,loadRequested=false,debugToggle=false;
                unsigned padMenuPresses=0;
                const auto resetMenuEdges=[&]{menuConfirm=menuBack=menuCombine=menuControls=debugToggle=false;padMenuPresses=0;seedSourceMenuDirections=true;};
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    controlInput.onEvent(event);
                    if (event.type == SDL_EVENT_QUIT) running = false;
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { focused = false;feedback.update(gamepad,false,world.chainsawFeedback()); world.actor.movement.held = false;world.resetNativeController();resetMenuEdges();inventoryNavigation.reset();settingsMenu.resetInput();seedMenuButtons(); }
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {focused=true;resetMenuEdges();settingsMenu.resetInput();seedMenuButtons();}
                    if (event.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad && event.gdevice.which == SDL_GetGamepadID(gamepad)) {
                        feedback.reset(gamepad);SDL_CloseGamepad(gamepad); gamepad = openFirstGamepad(); world.actor.movement.held = false;
                        world.resetNativeController();resetMenuEdges();seedMenuButtons();inventoryNavigation.reset();controlMenu.gamepadChanged();settingsMenu.resetInput();
                    }
                    if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) {
                        gamepad = SDL_OpenGamepad(event.gdevice.which);
                        world.resetNativeController();resetMenuEdges();seedMenuButtons();inventoryNavigation.reset();controlMenu.gamepadChanged();settingsMenu.resetInput();
                    }
                    if((event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||event.type==SDL_EVENT_GAMEPAD_BUTTON_UP)&&
                       (!gamepad||event.gbutton.which!=SDL_GetGamepadID(gamepad)))continue;
                    if(event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&
                       (!gamepad||event.gaxis.which!=SDL_GetGamepadID(gamepad)))continue;
                    if(focused&&DebugMenu::toggleKey(event))debugToggle=true;
                    if(focused&&debugMenu.onEvent(event)){controlInput.reset();resetMenuEdges();continue;}
                    if(controlMenu.onEvent(event)) {controlInput.reset();resetMenuEdges();continue;}
                    if(focused&&event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH)padMenuPresses|=1;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_EAST)padMenuPresses|=2;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_WEST)padMenuPresses|=4;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_NORTH)padMenuPresses|=8;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_LEFT)padMenuPresses|=0x10;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_RIGHT)padMenuPresses|=0x20;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_UP)padMenuPresses|=0x40;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_DOWN)padMenuPresses|=0x80;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_BACK)padMenuPresses|=0x1000;
                        if(event.gbutton.button==SDL_GAMEPAD_BUTTON_START)padMenuPresses|=0x2000;
                    }
                    if(focused&&((event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&(event.key.scancode==SDL_SCANCODE_I||event.key.scancode==SDL_SCANCODE_TAB))||
                       (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&event.gbutton.button==SDL_GAMEPAD_BUTTON_NORTH)))menuControls=true;
                    if(focused&&((event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_C)||
                       (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN&&event.gbutton.button==SDL_GAMEPAD_BUTTON_WEST)))menuCombine=true;
                    if(focused&&event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&
                       (event.key.scancode==SDL_SCANCODE_E||event.key.scancode==SDL_SCANCODE_RETURN||event.key.scancode==SDL_SCANCODE_KP_ENTER))menuConfirm=true;
                    if(focused&&event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat&&event.key.scancode==SDL_SCANCODE_ESCAPE) {
                        menuBack=true;
                    }
                }
                const Uint64 now=SDL_GetTicksNS();const double rawDt=double(now-previous)/1e9;const float dt=frameSeconds(rawDt);previous=now;
                bool advancedTime=false;
                const ControlFrame input=controlInput.poll(controlSettings,gamepad,focused);
                world.sampleNativeController(input);
                Vec2 padNavigation{},menuNavigation{};
                if (focused) {
                    const bool* keys=SDL_GetKeyboardState(nullptr);
                    const bool typing=debugMenu.wantsTextInput();
                    const Vec2 keyboard=radialStick(float((!typing&&keys[SDL_SCANCODE_D])||keys[SDL_SCANCODE_RIGHT])-float((!typing&&keys[SDL_SCANCODE_A])||keys[SDL_SCANCODE_LEFT]),
                        float((!typing&&keys[SDL_SCANCODE_S])||keys[SDL_SCANCODE_DOWN])-float((!typing&&keys[SDL_SCANCODE_W])||keys[SDL_SCANCODE_UP]),0);
                    menuNavigation={keyboard.x,-keyboard.z};
                    if(gamepad) {
                        const float zone=controlMenu.isOpen()?controlMenu.draft().deadzone():controlSettings.deadzone();
                        const Vec2 stick=radialStick(SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX)/32768.0f,
                            SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTY)/32768.0f,zone);
                        padNavigation={stick.x,-stick.z};
                        const Vec2 digital=radialStick(float(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT))-float(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_LEFT)),
                            float(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_DOWN))-float(SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_DPAD_UP)),0);
                        const Vec2 dpad{digital.x,-digital.z}; if(length(dpad)>length(padNavigation))padNavigation=dpad;
                        if(length(padNavigation)>length(menuNavigation))menuNavigation=padNavigation;
                    }
                }
                const bool padUse=pressedEdge(gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH),confirmHeld);
                const bool padCancel=pressedEdge(gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_EAST),cancelHeld);
                const bool padPause=pressedEdge(gamepad&&SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_BACK),pauseHeld);
                const unsigned directionDown=(menuNavigation.x<-.001f?0x10u:0u)|(menuNavigation.x>.001f?0x20u:0u)|
                    (menuNavigation.z<-.001f?0x40u:0u)|(menuNavigation.z>.001f?0x80u:0u);
                const unsigned directionPressed=seedSourceMenuDirections?0:directionDown&~sourceMenuDirections;
                sourceMenuDirections=directionDown;
                seedSourceMenuDirections=!focused;
                if(debugToggle&&!world.scene.menuContext&&!world.sourceMenuOwnsInput()&&!controlMenu.isOpen()) {
                    debugMenu.toggle();
                    if(debugMenu.open)try{debugMenu.items=world.debugItemCatalog();debugMenu.item=0;}catch(const std::exception& error){debugMenu.items.clear();debugMenu.notice=error.what();SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Debug catalog: %s",error.what());}
                    world.actor.movement.held=false;world.setOverlayPaused(debugMenu.open||pauseMenu.open||!focused);
                    controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                }
                if(debugMenu.open) {
                    if(!debugToggle&&focused&&(menuBack||padCancel||padPause)){debugMenu.back();controlInput.reset();inventoryNavigation.reset();}
                    else if(!debugToggle&&focused){debugMenu.move(inventoryNavigation.update(menuNavigation.x,menuNavigation.z,dt));if(menuConfirm||padUse)world.debugConfirm(debugMenu);}
                } else if(!controlMenu.isOpen()&&focused&&world.sourceOptionsMenuActive()&&menuControls) {
                    controlMenu.open(controlSettings);settingsMenu.resetInput();controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                } else if(controlMenu.isOpen()) {
                    controlMenu.step(padNavigation,dt,controlSettings,preferenceDirectory);
                    if(!controlMenu.isOpen()) {
                        if(sourceControlsPopup){world.sourceKeyboardMenuClose();sourceControlsPopup=false;}
                        settingsMenu.resetInput();
                        world.resetNativeController();
                        controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                    }
                } else if(settingsMenu.isOpen()&&!showOriginalOptions){
                    const auto action=settingsMenu.step(menuNavigation,focused?dt:0,
                        focused&&(menuConfirm||padUse),focused&&(menuBack||padCancel||padPause));
                    if(action==SettingsMenu::Action::Original)showOriginalOptions=true;
                    else if(action==SettingsMenu::Action::Language){
                        auto candidate=videoSettings;candidate.language=settingsMenu.draft().language;
                        if(applyVideoSettings(candidate))world.setUiLanguage(videoSettings.language);
                        settingsMenu.languageApplied(videoSettings.language);
                    }
                    else if(action==SettingsMenu::Action::Keyboard||action==SettingsMenu::Action::Gamepad)
                        controlMenu.open(controlSettings,action==SettingsMenu::Action::Gamepad?2:0);
                    else if(action==SettingsMenu::Action::Apply){
                        if(applyVideoSettings(settingsMenu.draft())){world.setUiLanguage(videoSettings.language);settingsMenu.applied(videoSettings);}
                    }else if(action==SettingsMenu::Action::ResetGame){
                        const auto before=world.game.checkpoint();const auto nativeBefore=world.checkpointNative();world.beginNativeStage();
                        try{
                            world.resetOriginalOptions();
                            if(!applyVideoSettings(settingsMenu.draft()))throw jpc::Error("Could not reset game settings");
                            world.setUiLanguage(videoSettings.language);settingsMenu.applied(videoSettings);world.commitNativeStage();
                        }catch(const std::exception& error){world.game.rollback(before);world.rollbackNative(nativeBefore);
                            settingsMenu.setNotice("could not apply settings");SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Game settings reset retained previous state: %s",error.what());}
                    }else if(action==SettingsMenu::Action::Back){
                        if(!dispatchSourceMenu(world,{102})){
                            settingsMenu.open(videoSettings,graphics.displayResolutions());settingsMenu.setNotice("could not use menu action");
                        }
                    }
                    if(action!=SettingsMenu::Action::None){
                        controlInput.reset();inventoryNavigation.reset();
                        if(action!=SettingsMenu::Action::Language)settingsMenu.resetInput();
                        seedMenuButtons();
                    }
                } else {
                    bool contextChanged=false;
                    const bool sourceMenu=world.sourceMenuOwnsInput();
                    const bool menuOwnsInput=(pauseMenu.open||world.inventoryView.open||sourceMenu)&&
                        (menuConfirm||menuBack||menuCombine||padUse||padCancel||padPause||length(menuNavigation)>0.001f);
                    if(!settingsMenu.isOpen()&&!menuOwnsInput&&input.pressed(ControlAction::Fullscreen)) {
                        auto candidate=videoSettings;candidate.fullscreen=(SDL_GetWindowFlags(graphics.window)&SDL_WINDOW_FULLSCREEN)==0;
                        if(!applyVideoSettings(candidate))world.notice="could not apply settings";
                        contextChanged=true;
                    }
                    if(!menuOwnsInput&&input.pressed(ControlAction::QuickSave)){world.saveGame(saveDirectory);contextChanged=true;}
                    if(!menuOwnsInput&&input.pressed(ControlAction::QuickLoad)){loadRequested=true;contextChanged=true;}
                    if(!world.game.profile.dreamcast&&!world.scene.menuContext&&!sourceMenu&&!menuOwnsInput&&input.pressed(ControlAction::Pause)) {
                        if(world.inventoryView.open)world.closeInventory();
                        else if(world.originalRuntime&&world.sourceGui.uvLayout)world.setInterfaceMode(3);
                        else pauseMenu.toggle();
                        world.actor.movement.held=false;inventoryNavigation.reset();contextChanged=true;
                    }
                    if(!world.game.profile.dreamcast&&!sourceMenu&&!menuOwnsInput&&!pauseMenu.open&&input.pressed(ControlAction::Inventory)) {
                        if(world.inventoryView.open)world.closeInventory();else world.openInventory();
                        inventoryNavigation.reset();contextChanged=true;
                    }
                    if(pauseMenu.open) {
                        if(!contextChanged&&focused&&(menuBack||padCancel||padPause)){pauseMenu.toggle();inventoryNavigation.reset();}
                        else if(!contextChanged&&focused) {
                            pauseMenu.move(inventoryNavigation.update(0,menuNavigation.z,dt));
                            if(menuConfirm||padUse) {
                                if(pauseMenu.selected==0)pauseMenu.toggle();
                                else if(pauseMenu.selected==1)world.saveGame(saveDirectory);
                                else if(pauseMenu.selected==2)loadRequested=true;
                                else if(pauseMenu.selected==3){controlMenu.open(controlSettings);controlInput.reset();}
                                else running=false;
                            }
                        }
                    } else if(world.inventoryView.open&&!sourceMenu) {
                        if(!contextChanged&&focused&&(menuBack||padCancel||padPause)) {
                            if(world.inventoryView.reading)world.closeDocument();else if(world.inventoryView.combineSlot>=0)world.clearInventoryCombination();else world.closeInventory();}
                        else if(!contextChanged&&focused) {
                            if(!world.inventoryView.reading)moveInventorySelection(world.inventoryView,inventoryNavigation.update(menuNavigation.x,menuNavigation.z,dt));
                            if(menuCombine)world.markInventoryCombination();
                            if(menuConfirm||padUse)world.applyInventoryItem();
                        }
                        world.step({},false,false,focused?dt:0,&graphics);advancedTime=focused;
                    } else if(sourceMenu) {
                        if(!contextChanged&&focused) {
                            const bool returnToSettings=showOriginalOptions&&world.sourceOptionsMenuActive()&&(menuBack||padCancel||padPause);
                            if(returnToSettings){
                                showOriginalOptions=false;settingsMenu.resetInput();controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                            }
                            std::vector<int> events;
                            const bool sourceInventory=world.game.vm.staticField(844).integer()==2;
                            if(!returnToSettings&&world.game.profile.dreamcast) {
                                // Native GUI producer: ascending logical rows, physical press edges only.
                                if(padUse||(padMenuPresses&1))world.enqueueNativeInput(99,1);
                                if(menuBack||padCancel||(padMenuPresses&2))world.enqueueNativeInput(100,2);
                                if(menuCombine||(padMenuPresses&4))world.enqueueNativeInput(101,4);
                                if(padMenuPresses&8)world.enqueueNativeInput(102,8);
                                for(unsigned n=0;n<4;++n)if((directionPressed|padMenuPresses)&(0x10u<<n))world.enqueueNativeInput(103+int(n),0x10u<<n);
                                if(menuConfirm)world.enqueueNativeInput(world.sourceMenuConfirmEvent(),1);
                                // View is a modern Back shortcut; the original pad had only Start.
                                if(padPause||(padMenuPresses&0x1000))world.enqueueNativeInput(100,0x1000);
                                if(input.pressed(ControlAction::Inventory)||(padMenuPresses&0x2000))world.enqueueNativeInput(112,0x2000);
                            } else if(!returnToSettings){
                                if(menuBack||padCancel||padPause||(sourceInventory&&input.pressed(ControlAction::Inventory)))events.push_back(102);
                                else {
                                    const int navigation=inventoryNavigation.update(menuNavigation.x,menuNavigation.z,dt);
                                    if(navigation)events.push_back(navigation==-1?103:navigation==1?104:navigation<0?105:106);
                                    if(menuConfirm||padUse||(sourceInventory&&menuCombine))events.push_back(world.sourceMenuConfirmEvent());
                                }
                            }
                            if(!events.empty())dispatchSourceMenu(world,events);
                        }
                        world.step({},false,false,focused?dt:0,&graphics);advancedTime=focused;
                    } else if(!contextChanged) {
                        world.setOverlayPaused(!focused);
                        if(focused)world.playerControls(input);
                        const bool aimedUse=focused&&input.pressed(ControlAction::Use)&&input.held(ControlAction::Aim)&&world.actors&&world.game.vm.field(world.game.ash,123).integer()!=0;
                        world.step(input.movement,input.held(ControlAction::Run),input.pressed(ControlAction::Use),focused?dt:0,&graphics);
                        if(world.takeNativeAimCancellation()||(!world.game.profile.dreamcast&&aimedUse&&!world.scriptFault&&!world.game.vm.field(world.game.ash,123).integer()))controlInput.cancelAimUntilRelease();
                        advancedTime=focused;
                    }
                }
                if(world.takeNativeModeChange()){controlInput.reset();inventoryNavigation.reset();seedMenuButtons();}
                const bool wantsTextInput=focused&&debugMenu.wantsTextInput();
                if(wantsTextInput!=debugTextInputActive){
                    if(wantsTextInput)debugTextInputActive=SDL_StartTextInput(graphics.window);
                    else{SDL_StopTextInput(graphics.window);debugTextInputActive=false;}
                }
                world.setOverlayPaused(debugMenu.open||controlMenu.isOpen()||(settingsMenu.isOpen()&&!showOriginalOptions)||pauseMenu.open||!focused||world.scriptFault);
                if(!controlMenu.isOpen()&&world.sourceKeyboardMenuActive()) {
                    controlMenu.open(controlSettings);sourceControlsPopup=true;controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                }
                // EDSystem owns paused game time; sound streams and the raw clock continue.
                world.advanceIdleTime(std::max(0.0,rawDt-(advancedTime?double(dt):0.0)));
                if(world.sourceCart&&world.sourceCart->pending()&&world.sourceCart->checkpoint().pending->operation==edcart::Operation::Access)
                    processSourceCart(world);
                if(observedMovies!=presentedMovies||observedScreenRevision!=world.screenPresentationRevision){observedMovies=presentedMovies;observedScreenRevision=world.screenPresentationRevision;controlInput.reset();seedMenuButtons();inventoryNavigation.reset();previous=SDL_GetTicksNS();frameGate=edclock::FrameGate(uint32_t(SDL_GetTicks()));}
                if(world.originalRuntime&&!advancedTime){world.sampleSourceScreen();world.sampleSourceTexts();}
                world.inventoryView.notice=world.notice;
                SDL_SetWindowTitle(graphics.window,"Evil Dead: Hail to the King");
                const bool utilityOverlay=debugMenu.open||controlMenu.isOpen()||(settingsMenu.isOpen()&&!showOriginalOptions)||pauseMenu.open;
                const auto chainsaw=world.chainsawFeedback();
                const bool chainsawActive=focused&&!utilityOverlay&&!world.scriptFault&&chainsaw.gameplay&&chainsaw.running;
                const int idleVoice=chainsawActive?world.game.vm.field(world.game.vm.field(world.game.ash,102).reference(),168).integer():-1;
                world.audioBank.presentChainsaw(idleVoice,chainsawActive&&input.held(ControlAction::ChainsawRev),chainsawActive?chainsaw.attackPitch:0,rawDt);
                feedback.update(gamepad,focused&&!utilityOverlay&&!world.scriptFault,chainsaw);
                static const std::vector<edguitext::Page> noSourcePages;
                const bool russian=russianUi(videoSettings.language);
                auto guiCommands=utilityOverlay?utilityUi.sample(debugMenu.open?debugMenu.presentation(russian):
                    controlMenu.isOpen()?controlMenu.presentation(russian):
                    settingsMenu.isOpen()&&!showOriginalOptions?settingsMenu.presentation(russian):pauseMenu.presentation(world.notice,russian),world.rawClockSeconds):
                    world.sourceGuiCommands;
                const auto hud=utilityOverlay?std::string{}:world.hud(russian);
                if(world.originalRuntime&&!hud.empty()){
                    auto noticeCommands=utilityUi.notice(hud,world.rawClockSeconds);
                    guiCommands.insert(guiCommands.end(),noticeCommands.begin(),noticeCommands.end());
                }
                if (graphics.render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,
                    world.actor.animationTime,world.actor.walking,world.originalRuntime?std::string{}:hud,
                    world.inventoryView.open&&!world.authoredUiComplete&&!world.sourceMenuOwnsInput()&&!utilityOverlay?&world.inventoryView:nullptr,world.actors.get(),
                    world.drawObjects(),world.drawEffects(),world.drawActorTints(),utilityOverlay?edscreen::Overlay{}:world.lastOverlay,world.lastProjectionShake,
                    utilityOverlay?noSourcePages:world.sourceTextPages,utilityOverlay?std::vector<edguipresentation::Sprite>{}:world.drawGuiSprites(),
                    guiCommands)) ++frames;
                if (frames >= 3 && !screenshot.empty()) { graphics.screenshot(screenshot); screenshot.clear(); }
                if (smoke && frames >= 8) running = false;
                if(world.quitRequested)running=false;
                if(world.sourceCart&&world.sourceCart->pending()) {
                    auto replacement=processSourceCart(world,&graphics);
                    if(replacement) {
                        feedback.reset(gamepad);
                        ownedWorld=std::move(replacement);controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                        previous=SDL_GetTicksNS();frameGate=edclock::FrameGate{uint32_t(SDL_GetTicks())};
                        observedScreenRevision=ownedWorld->screenPresentationRevision;observedMovies=presentedMovies;
                    }
                } else if(loadRequested) {
                    try {
                        const auto bytes=savefile::read(saveDirectory,"quicksave.eds");
                        statebytes::Reader header{bytes};if(header.text()!="EDPLAY1")throw jpc::Error("Unsupported saved world header");
                        header.get<uint32_t>();const auto identity=header.text();
                        if(!world.originalRuntime&&world.containsScene(identity))world.restoreStateBytes(bytes,&graphics);
                        else {
                            Graphics::ProfileScope profile(graphics);
                            auto next=stageLoadGame(world,bytes);auto prepared=graphics.prepare(next->scene,next->directory());
                            graphics.loadFont(next->root);graphics.loadInventory(next->root);
                            graphics.loadActorTextures(next->root,*next->actors);graphics.prepareCameraFrame(*prepared,next->scene,next->actor.camera);
                            next->audio.openDevice();next->audioBank.openDevice();if(next->streams)next->streams->openDevice();
                            graphics.commit(std::move(prepared));profile.commit();feedback.reset(gamepad);ownedWorld=std::move(next);
                        }
                    } catch(const std::exception& error){world.notice="could not load game";world.noticeDoor=-1;SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Load retained current world: %s",error.what());}
                    controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                    previous=SDL_GetTicksNS();frameGate=edclock::FrameGate{uint32_t(SDL_GetTicks())};
                    observedScreenRevision=ownedWorld->screenPresentationRevision;observedMovies=presentedMovies;
                } else if(world.pendingSceneSwitch) {
                    const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
                    if(!edpackage::reload(request.sourceSceneId,request.destinationId))world.switchScene(request,&graphics);
                    else {
                        const auto screenBefore=world.screenSnapshot();
                        const bool muted=world.audioBank.muted(),streamMuted=world.streams&&world.streams->muted();
                        const float doorGain=world.audio.stream?SDL_GetAudioStreamGain(world.audio.stream):1;
                        try {
                            world.sourceScreen.fadeOut();
                            if(edpackage::restart(request.destinationId))world.waitForScreenFade(&graphics);
                            world.audioBank.mute(true);if(world.streams)world.streams->mute(true);
                            if(world.audio.stream)SDL_SetAudioStreamGain(world.audio.stream,0);
                            Graphics::ProfileScope profile(graphics);
                            auto next=stagePackageWorld(world,request,{},true);
                            auto prepared=graphics.prepare(next->scene,next->directory());
                            graphics.loadFont(next->root);graphics.loadInventory(next->root);
                            graphics.loadActorTextures(next->root,*next->actors);
                            graphics.prepareCameraFrame(*prepared,next->scene,next->actor.camera);
                            next->sourceScreen.fadeIn();next->transitions=world.transitions+1;next->trace=world.trace;
                            next->audio.openDevice();next->audioBank.openDevice();if(next->streams)next->streams->openDevice();
                            graphics.commit(std::move(prepared));profile.commit();feedback.reset(gamepad);ownedWorld=std::move(next);
                        } catch(const std::exception& error) {
                            world.restoreScreen(screenBefore);world.audioBank.mute(muted);
                            if(world.streams){world.streams->mute(streamMuted);world.streams->commit();}
                            if(world.audio.stream)SDL_SetAudioStreamGain(world.audio.stream,doorGain);
                            world.notice="could not load area";world.noticeDoor=-1;
                            for(size_t i=0;i<world.scene.doors.size();++i)if(world.scene.doors[i].destination==edpackage::sceneName(request.destinationId))world.failedPortal=int(i);
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Package load retained current world: %s",error.what());
                        }
                    }
                    controlInput.reset();inventoryNavigation.reset();seedMenuButtons();
                    previous=SDL_GetTicksNS();frameGate=edclock::FrameGate{uint32_t(SDL_GetTicks())};
                    observedScreenRevision=ownedWorld->screenPresentationRevision;observedMovies=presentedMovies;
                }
                SDL_Delay(1);
            }
            if(debugTextInputActive)SDL_StopTextInput(graphics.window);
            feedback.reset(gamepad);if (gamepad) SDL_CloseGamepad(gamepad);gamepad=nullptr;
        }
        SDL_Quit(); return 0;
    } catch(const QuitRequested&) {SDL_Quit();return 0;}
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n'; SDL_Quit(); return 1;
    }
}
