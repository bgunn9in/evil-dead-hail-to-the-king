#pragma once
#include "script_game.h"
#include "actor_actions.h"
#include "source_playback_pool.h"
#include "source_animation_height.h"
#include "actor_mesh.h"
#include "actor_catalog.h"
#include "state_bytes.h"
#include "source_actor_math.h"
#include "source_animation_event.h"
#include "source_animation_sound.h"
#include <cmath>
#include <memory>

namespace edactors {
inline constexpr uint32_t MaxBodies=44; // Ash1 + nine NPC pools4 + seven singleton NPCs.
struct Key {
    int type=0,index=0;
    bool operator<(const Key& other) const {return std::tie(type,index)<std::tie(other.type,other.index);}
    bool operator==(const Key& other) const {return type==other.type&&index==other.index;}
};
struct Pose {float x=0,y=0,z=0,yaw=0;};
struct Body {
    Key key;jpc::Ref script=0;Pose pose;
    float localY=0;
    std::array<float,3> pendingMove{}; // Native aux+B4: local while motionFlags&4, world otherwise.
    int flags=63,radius=0,fov=0,sight=0; // Native401160: flags3e, then visible=true.
    std::vector<bool> parts;
    edactions::ActorActions actions;
    edmesh::LocalPose localPose;
    std::vector<edmesh::Vertex32> vertices;
    jpc::Interpreter::ThreadID think=0;
    uint32_t allocationOrder=0;
    uint8_t motionFlags=6,motionFlags2=0; // Native413760 aux+E8/E9, separate from external actor flags.
    int32_t floorTriangle=-1; // Native actor+104 BV cache, distinct from shared46e61c.
    float cachedFloorHeight=0; // Native actor+100; refreshed by the render-phase BV query.
    uint32_t textureID=0;
    int32_t nativeSlot=-1; // Physical context+938 registration; independent of arena allocation order.
    edanimsound::Materials sfxMaterial{}; // Native41A355 actoraux+10 initializes four source surface codes tozero.
    uint32_t meshResource=0;
    Body(Key identity,Pose position,const std::vector<edactions::State>& source,const edmesh::ActorMesh& mesh,const ModelDefinition& model,edactions::PoolHooks hooks={},bool initialize=true)
        :key(identity),pose(position),parts(mesh.partCount,true),actions(source,std::move(hooks),initialize),localPose(mesh.restPose()),textureID(model.texture),meshResource(model.mesh){}
};
// Raw native message fields: their interpretation depends on the producer.
// Native40d490 matches only first pair/event; it ignores the second pair.
struct Message {
    Key first,second{-1,-1};int event=0;
    std::array<int,4> data{};
};
struct PickupObject {int index=0;bool active=false;float x=0,z=0;std::array<int,4> setup{};float y=0;};
struct PickupTarget {int index=-1;std::array<int,4> setup{};std::array<float,3> position{};};
struct PickupState {
    PickupTarget query,target,receipt;uint64_t queryTick=0;bool queried=false;
    int playback=-1,state=-1;
    uint8_t receiptPhase=0; // 0none,1queued,2cancelled; original event17 still owns receive.
};
struct Host {
    // Native actorCreate receives original mesh/skeleton/texture IDs. A marker
    // is set by later source activation; this callback supplies initial XYZ0.
    std::function<Pose(Key,int,int,int)> spawnPose;
    std::function<float(float,float)> floorHeight;
    std::function<bool(Key,Key)> lineOfSight;
    std::function<bool(Key,int,int)> lineOfSightDirection; // Native fixed-point displacement components, not distance/angle.
    std::function<bool(Key)> visibleArea;
    std::function<bool(Key,Key,int)> facing;
    std::function<int(Body&,int,int,int)> move; // Native forward/strafe/vertical arguments, not an invented AI route.
    std::function<void(Body&)> applyMovement; // Resolve pending displacement at native403233/413600 phase.
    std::function<bool(Key,const edactions::State&)> rootMotionAllowed;
    std::function<int()> turnAmount; // Native8c0c6a48 degrees, shared by actor callbacks1/2.
    std::function<std::vector<PickupObject>()> pickupObjects;
    std::function<int(int)> random;
    std::function<int(const std::string&,const std::vector<int>&)> effect;
    std::function<float(Key,float,float,int32_t&)> floorHeightForActor;
    std::function<void(const Body&,const std::array<uint8_t,36>&)> animationEvent;
    std::function<void(const Body&,const std::array<uint8_t,20>&)> animationSound;
    std::function<edanimsound::Materials(const Body&)> soundMaterials;
    std::function<void()> soundFrame;
    std::function<bool()> playerDamageAllowed; // Optional session debug policy; absent preserves source dispatch.
};
struct Interest {int listenerType=0,firstType=0,firstIndex=0,event=0;std::set<int> indices;};
struct Dispatch {Key listener;Message message;jpc::Interpreter::ThreadID thread=0;};
struct BodyState {
    Key key;jpc::Ref script=0;Pose pose;
    float localY=0;
    std::array<float,3> pendingMove{};
    int flags=0,radius=0,fov=0,sight=0;std::vector<bool> parts;
    std::array<edactions::Layer,7> layers;
    std::array<edactions::Playback,50> playbacks;
    std::vector<edactions::Event> events;uint64_t updates=0;
    edmesh::LocalPose localPose;jpc::Interpreter::ThreadID think=0;uint32_t allocationOrder=0;
    uint8_t motionFlags=6,motionFlags2=0;int32_t floorTriangle=-1;
    float cachedFloorHeight=0;
    uint32_t textureID=0;
    int32_t nativeSlot=-1;
    edanimsound::Materials sfxMaterial{};
    uint32_t meshResource=0;
};
struct State {
    std::vector<BodyState> bodies;std::vector<Interest> interests;
    std::vector<Message> messages;std::vector<Dispatch> callbacks;
    uint64_t sourceTick=0;bool updated=false;
    uint32_t levelBoundary=0;bool boundaryCaptured=false;
    bool globalPlayback=true;edactions::SourcePlaybackPool::State playbackPool{};
    edactions::AnimationHeight animationHeight{};
    PickupState pickup{};
};
class GameplayActors {
    struct Asset {const ModelDefinition* definition;std::vector<edactions::State> actions;edmesh::ActorMesh mesh;
        std::map<uint32_t,std::vector<std::array<uint8_t,36>>> animationEvents;
        std::map<uint32_t,std::vector<std::array<uint8_t,20>>> animationSounds;};
    ScriptGame& game;Host host;
    std::map<uint32_t,Asset> assets;
    std::set<int> loadedTypes;
    bool levelB=false;
    bool globalPlayback=true;edactions::SourcePlaybackPool playbackPool;
    edactions::AnimationHeight animationHeight;
    PickupState pickup;
    std::set<uint32_t> textureIDs;
    std::map<Key,Body> actors;
    std::vector<Interest> interests;
    std::vector<Message> messages;
    std::vector<Dispatch> callbacks;
    uint64_t sourceTick=0;bool updated=false;
    uint32_t levelBoundary=0;bool boundaryCaptured=false;
    edactions::PoolHooks poolHooks(Key key){
        if(!globalPlayback)return {};
        const edactions::PlaybackOwner owner{key.type,key.index};
        return {[this,owner](bool priority){return playbackPool.allocate(owner,priority);},
            [this,owner](unsigned slot,const edactions::Playback& p){playbackPool.changed(owner,slot,p.active,p.reusable);},
            [this,key](const edactions::Event& event){queue({key,{-1,-1},event.event,{}});},
            [this](unsigned slot){
                const auto targetOwner=playbackPool.at(slot).owner;
                //4055c0 can clear the record behind a retained historical pointer.
                // Its dead mode byte is overwritten before any later allocation uses it.
                if(targetOwner.empty())return;
                auto& actions=at({targetOwner.type,targetOwner.index}).actions;auto& child=actions.playbacks.at(slot);
                //4063f2 follows the current GLOBAL record, including foreign reuse.
                if(child.child>=0){child.mode=3;child.frame=actions.states.at(size_t(child.state)).finishStart;}
                else child.mode=child.desiredMode;
                playbackPool.changed(targetOwner,slot,child.active,child.reusable);}};
    }
    void setFloorHeight(Body& body,int32_t centimeters) {
        body.localY=float(centimeters)*0.01f;
        if(!host.floorHeightForActor)body.cachedFloorHeight=actorFloor(body.key,body.pose.x,body.pose.z,body.floorTriangle);
        body.pose.y=body.cachedFloorHeight+body.localY;
    }
    void animationRootHeight(Body& body,const edactions::Playback& playback) {
        const auto& state=body.actions.states.at(size_t(playback.state));
        animationHeight.beforeFrame({body.key.type,body.key.index},state.clipFlags,playback.frame,state.start,state.end,
            game.profile.dreamcast,[&]{
                auto first=playback,last=playback;first.frame=state.start;last.frame=state.end;
                const auto& mesh=asset(body).mesh;
                return mesh.rootPosition(state.animation,edactions::curveFrame(state,last))[1]-
                       mesh.rootPosition(state.animation,edactions::curveFrame(state,first))[1];
            },[&](int32_t cm){setFloorHeight(body,cm);});
    }
    static jpc::Value I(int32_t n){return jpc::Value::integer(n);}
    static jpc::Value R(jpc::Ref n){return jpc::Value::reference(n);}
    template<class Fn> static Fn& required(Fn& fn,const char* name) {
        if(!fn)throw jpc::Error(std::string("Missing actor host capability: ")+name);return fn;
    }
    static void valid(Pose pose) {
        for(float value:{pose.x,pose.y,pose.z,pose.yaw})if(!std::isfinite(value))throw jpc::Error("Non-finite native actor pose");
    }
    static int32_t fixed(float position) {
        const double value=double(position)*100;
        if(!std::isfinite(value)||value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())
            throw jpc::Error("Actor position exceeds native fixed-point range");return int32_t(value);
    }
    void loadTextures(const std::filesystem::path& root) {
        for(const auto& [id,source]:assets){(void)id;textureIDs.insert(source.definition->texture);}
        const auto directory=root/"actors/textures";std::ifstream input(directory/"manifest.txt");if(!input)return;
        std::string magic;uint32_t count=0;if(!(input>>magic>>count)||magic!="EDACTORTEX1"||!count||count>256)throw jpc::Error("Invalid actor texture manifest");
        std::set<uint32_t> found;
        for(uint32_t n=0;n<count;++n) {
            std::string token;if(!(input>>token)||token.size()!=8)throw jpc::Error("Invalid actor texture ID");
            size_t used=0;uint32_t id=0;try{id=uint32_t(std::stoul(token,&used,16));}catch(const std::exception&){throw jpc::Error("Invalid actor texture ID");}
            if(used!=8||!id||(id&0xe0000000)||!found.insert(id).second||!std::filesystem::is_regular_file(directory/(token+".rgba")))
                throw jpc::Error("Missing/duplicate actor texture resource");
        }
        std::string trailing;if(input>>trailing)throw jpc::Error("Trailing actor texture manifest data");
        for(uint32_t id:textureIDs)if(!found.count(id))throw jpc::Error("Actor texture manifest omits source default");textureIDs.swap(found);
    }
    static void loadAnimationEvents(Asset& asset,const std::filesystem::path& path,bool required) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file){if(required)throw jpc::Error("Missing original animation events");return;}
        const auto size=file.tellg();if(size<12||uint64_t(size)>2*1024*1024)throw jpc::Error("Invalid original animation event size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw jpc::Error("Truncated original animation events");
        statebytes::Reader in{bytes};for(char c:std::string("EDANEV1\0",8))if(in.get<uint8_t>()!=uint8_t(c))throw jpc::Error("Invalid original animation events header");
        const auto clips=in.count(4096);if(clips!=asset.mesh.clips.size())throw jpc::Error("Animation event/mesh clip count mismatch");
        for(uint32_t n=0;n<clips;++n) {
            const auto id=in.get<uint32_t>(),n36=in.count(4096),n20=in.count(4096);asset.mesh.clip(id);
            auto [entry,inserted]=asset.animationEvents.emplace(id,std::vector<std::array<uint8_t,36>>{});if(!inserted)throw jpc::Error("Duplicate animation event clip");
            for(uint32_t i=0;i<n36;++i){std::array<uint8_t,36> raw{};for(auto& b:raw)b=in.get<uint8_t>();
                // Original Ash also contains records beyond the clip timeline;
                // native exact-frame matching leaves those records inert.
                edanimevent::decode(raw);entry->second.push_back(raw);}
            auto& sounds=asset.animationSounds[id];
            for(uint32_t i=0;i<n20;++i){std::array<uint8_t,20> raw{};for(auto& b:raw)b=in.get<uint8_t>();
                edanimevent::decodeSound(raw);sounds.push_back(raw);}
        }
        in.end();
    }
    float actorFloor(Key key,float x,float z,int32_t& cache) {
        const float height=host.floorHeightForActor?host.floorHeightForActor(key,x,z,cache):required(host.floorHeight,"floorHeight")(x,z);
        if(!std::isfinite(height)||cache< -1||cache>=100000)throw jpc::Error("Invalid native actor floor/cache");return height;
    }
    Asset& asset(uint32_t id) {auto found=assets.find(id);if(found==assets.end())throw jpc::Error("Unsupported original actor mesh: "+std::to_string(id));return found->second;}
    Asset& asset(const Body& body){return asset(body.meshResource);}
    jpc::Ref script(Key key) const {
        const auto& offsets=game.vm.arrayValues(game.vm.staticField(54).reference());
        if(key.type<0||size_t(key.type)>=offsets.size())throw jpc::Error("Invalid ActorDB type");
        const auto offset=offsets[size_t(key.type)].integer();const auto& pool=game.vm.arrayValues(game.vm.staticField(53).reference());
        if(offset<0||key.index<0||uint64_t(offset)+unsigned(key.index)>=pool.size())throw jpc::Error("Invalid ActorDB slot");
        return pool[size_t(offset+key.index)].reference();
    }
    void compose(Body& body) {
        const auto& mesh=asset(body).mesh;
        for(const auto& playback:body.actions.playbacks)if(playback.active&&playback.mode!=4) {
            const auto& state=body.actions.states.at(size_t(playback.state));
            mesh.applyPose(body.localPose,state.animation,edactions::curveFrame(state,playback),state.boneMask);
        }
        body.vertices=mesh.skinPose(body.localPose,body.parts);
    }
    std::vector<Body*> ordered() {
        std::vector<Body*> result;for(auto& [key,body]:actors){(void)key;result.push_back(&body);}
        std::sort(result.begin(),result.end(),[](auto* a,auto* b){return a->allocationOrder<b->allocationOrder;});return result;
    }
    std::vector<Body*> registered() {
        std::vector<Body*> result;for(auto& [key,body]:actors){(void)key;if(body.nativeSlot>=0)result.push_back(&body);}
        std::sort(result.begin(),result.end(),[](auto* a,auto* b){return a->nativeSlot<b->nativeSlot;});return result;
    }
    int distance(const Body& from,const Body& target) const {
        const double x=double(target.pose.x)-from.pose.x,z=double(target.pose.z)-from.pose.z,squared=x*x+z*z;
        double d=(squared==0?1:std::sqrt(squared))*100; // Native421b60 returns1 at zero length.
        if(d>target.radius)d-=target.radius;
        if(d>std::numeric_limits<int>::max())throw jpc::Error("Actor distance exceeds native range");return int(d);
    }
    static bool facesPoint(const Pose& pose,float x,float z,int degrees) {
        const double threshold=std::abs(double(degrees))*3.14159265358979323846/180;
        if(threshold>=3.14159265358979323846)return true;
        const float heading=float(std::atan2(double(z)-pose.z,double(x)-pose.x));
        const double delta=std::remainder(double(edactormath::modelYawFromHeading(heading))-pose.yaw,double(edactormath::Tau));
        return std::abs(int(delta))<threshold; // Original404a70 integer-abs quirk.
    }
    static bool pickupAnimation(uint32_t clip){return clip==0x01824784||clip==0x018247c4||clip==0x01824804;}
    static PickupTarget pickupTarget(const PickupObject& object){return {object.index,object.setup,{{object.x,object.y,object.z}}};}
    bool pickupMatches(const PickupTarget& target) const {
        if(target.index<0)return false;
        for(const auto& object:required(host.pickupObjects,"pickupObjects")())if(object.index==target.index)
            return object.active&&object.setup==target.setup&&std::array<float,3>{{object.x,object.y,object.z}}==target.position;
        return false;
    }
    void clearPickupTarget(){pickup.target={};pickup.playback=pickup.state=-1;}
    void clearPickupReceipt(){pickup.receipt={};pickup.receiptPhase=0;}
    bool reservedPickupMessage(Key listener,const Message& message) const {
        return pickup.receiptPhase&&listener==Key{0,0}&&message.first==Key{0,0}&&message.second==Key{-1,-1}&&
            message.event==17&&message.data[0]==pickup.receipt.index;
    }
    void refreshPickupTarget(const Body& body) {
        if(pickup.playback<0||body.key.type!=0||body.key.index!=0)return;
        const auto& play=body.actions.playbacks.at(size_t(pickup.playback));
        const bool attached=std::any_of(body.actions.layers.begin(),body.actions.layers.end(),[&](const auto& layer){return layer.playback==pickup.playback&&layer.state==pickup.state;});
        if(!play.active||play.state!=pickup.state||!attached)clearPickupTarget();
    }
    bool command(Body& body,int action) {
        const bool accepted=body.actions.command(action);
        if(!game.profile.dreamcast||body.key.type!=0||body.key.index!=0)return accepted;
        refreshPickupTarget(body);
        if(action>=115&&action<=117) {
            const auto query=pickup.query;const bool current=pickup.queried&&pickup.queryTick==sourceTick;
            pickup.query={};pickup.queryTick=0;pickup.queried=false;
            if(accepted) {
                clearPickupTarget();
                if(current)for(const auto& layer:body.actions.layers)if(layer.state>=0&&layer.playback>=0&&
                    pickupAnimation(body.actions.states.at(size_t(layer.state)).animation)) {
                    const auto& play=body.actions.playbacks.at(size_t(layer.playback));
                    if(play.active&&play.state==layer.state){if(pickupMatches(query))pickup.target=query;pickup.playback=layer.playback;pickup.state=layer.state;break;}
                }
            }
        }
        return accepted;
    }
    std::vector<int> pickupCandidates(const Body& body) const {
        auto objects=required(host.pickupObjects,"pickupObjects")();if(objects.size()>32)throw jpc::Error("Object contact pool exceeds source32");
        std::sort(objects.begin(),objects.end(),[](const auto& a,const auto& b){return a.index<b.index;});
        // DC05ece0 stores meters;0601a0 and animation-contact07a4a0 compare
        // float centimeters without truncating the radius (80 ->79.999992f).
        const bool dreamcast=game.profile.dreamcast;
        const float radiusMeters=float(body.radius)*.01f,radiusCm=radiusMeters*100.f;
        const int legacyRadius=int(double(float(double(body.radius)*.01f))*100);
        std::vector<int> result;int previous=-1;
        for(const auto& object:objects) {
            if(object.index<0||object.index>=32||object.index==previous||!std::isfinite(object.x)||!std::isfinite(object.y)||!std::isfinite(object.z))
                throw jpc::Error("Invalid native pickup object");previous=object.index;
            if(!object.active)continue;
            const float x=float(double(object.x)-body.pose.x),z=float(double(object.z)-body.pose.z);
            // DC05ef20: float subtraction, FMUL+FMAC length, FSQRT, float*100, FTRC.
            const float nativeSquared=std::fma(x,x,float(z*z));
            const double squared=double(x)*x+double(z)*z;
            const double distance=dreamcast?double(float((nativeSquared==0?1.f:std::sqrt(nativeSquared))*100.f)):
                (squared==0?1:std::sqrt(squared))*100;
            if(distance>=150)continue;const int centimeters=int(distance);
            if((dreamcast?float(centimeters)<radiusCm:centimeters<legacyRadius)||facesPoint(body.pose,object.x,object.z,45))result.push_back(object.index);
        }
        return result;
    }
    void contact(Body& ash,int type,int playback=-1) {
        if(ash.key.type!=0)return;
        if(type==2||type==3) {
            for(auto* victim:registered())if(victim->key.type!=0&&(victim->flags&1)&&distance(ash,*victim)<250&&
                required(host.facing,"facing")(ash.key,victim->key,30))
                queue({victim->key,ash.key,5,{{type==2?-1:-2,0,0,0}}});
        } else if(type==4) {
            if(game.profile.dreamcast&&pickup.playback==playback&&pickup.playback>=0) {
                // Keep the accepted target through the pickup clip's backward root motion.
                const auto target=pickup.target;clearPickupTarget();
                // One outstanding reserved17 owns its fingerprint until the source accepts it.
                if(!pickup.receiptPhase&&pickupMatches(target)&&queue({ash.key,{-1,-1},17,{{target.index,0,0,0}}})){
                    pickup.receipt=target;pickup.receiptPhase=1;
                }
                return;
            }
            for(int object:pickupCandidates(ash))queue({ash.key,{-1,-1},17,{{object,0,0,0}}});
        }
    }
    void animationContacts(Body& body,const std::array<edactions::Playback,50>& before,int only=-1) {
        if(body.key.type!=0)return;
        for(unsigned index=0;index<50;++index) {
            if(only>=0&&index!=unsigned(only))continue;
            const auto& p=body.actions.playbacks[index];if(!p.active||p.mode==4||before[index].state!=p.state)continue;
            const auto& s=body.actions.states.at(size_t(p.state));if(p.frame!=(int(s.start)+s.earlyEnd)/2)continue;
            int kind=0;
            if(pickupAnimation(s.animation))kind=4;
            // shortcut: PC40648b hardcodes absent melee IDs; bind its midpoint
            // formula to the actual source Axe/Chainsaw basic clips instead.
            else if(s.animation==0x01824f44||s.animation==0x01824f48||s.animation==0x01824f4c)kind=2;
            else if(s.animation==0x01824fc4||s.animation==0x01824fc8||s.animation==0x01824fcc)kind=3;
            if(kind)contact(body,kind,int(index));
        }
    }
    void animationEvents(const Body& body,const edactions::Playback& playback) {
        const bool tree=body.key.type==5||body.key.type==34,variant=body.key.type==14||body.key.type==17||body.key.type==18;
        const bool blood21=body.key.type==7||body.key.type==8||body.key.type==11||body.key.type==12||
            body.key.type==19||body.key.type==23||body.key.type==26||body.key.type==31;
        const bool npc20=body.key.type==2||body.key.type==4||body.key.type==7||body.key.type==8||body.key.type==9||
            body.key.type==10||body.key.type==11||body.key.type==12||body.key.type==23;
        const bool soundsEnabled=edanimsound::verifiedActor(body.key.type),visual=tree||variant||body.key.type==9||body.key.type==0||
            body.key.type==4||body.key.type==35||blood21||npc20;
        // shortcut: raw36 covers verified Ash/Tree records and TroopRange7;
        // other raw visuals remain preserved independently of verified sound20.
        if((!soundsEnabled&&!visual)||!playback.active||playback.mode==4)return;
        const auto& source=asset(body);
        const auto id=body.actions.states.at(size_t(playback.state)).animation;
        if(soundsEnabled){const auto sounds=source.animationSounds.find(id);if(sounds!=source.animationSounds.end())
            for(const auto& raw:sounds->second){const auto sound=edanimevent::decodeSound(raw);
                if(sound.enabled&&sound.frame==playback.frame)required(host.animationSound,"animationSound")(body,raw);}}
        if(!visual)return;
        const auto found=source.animationEvents.find(id);if(found==source.animationEvents.end())return;
        for(const auto& raw:found->second){const auto event=edanimevent::decode(raw);
            const bool verified=tree?(event.kind==0||event.kind==9||event.kind==15||event.kind==24||event.kind==25||event.kind==35):variant?
                ((event.mode==1&&((body.key.type==17&&id==0x038c1404&&event.frame==32&&event.kind==25)||
                (body.key.type==14&&(id==0x03b11184||id==0x03b111c4)&&event.frame==23&&event.kind==24)||
                (body.key.type==18&&id==0x03833184&&event.frame==10&&event.kind==24)))||
                    (body.key.type==14&&event.mode==-1&&(event.kind==9||event.kind==16))):
                ((body.key.type==0||body.key.type==9)&&event.kind==7)||
                (body.key.type==0&&(event.kind==0||event.kind==6||event.kind==8||event.kind==9||event.kind==20||event.kind==35))||
                (event.mode==-1&&(((body.key.type==4||body.key.type==35)&&event.kind==17)||
                    (body.key.type==4&&event.kind==16)||(blood21&&event.kind==21)||(npc20&&event.kind==20)));
            if(verified&&event.enabled&&event.frame==playback.frame)required(host.animationEvent,"animationEvent")(body,raw);}
    }
    void actionCallbacks(Body& body) {
        //8c078540 dispatches each active layer before actor root extraction.
        for(const auto& layer:body.actions.layers)if(layer.state>=0) {
            const auto callback=body.actions.states.at(size_t(layer.state)).callback;
            if(callback!=1&&callback!=2)continue;
            const int amount=host.turnAmount?host.turnAmount():8;
            if(amount<0||amount>12)throw jpc::Error("Invalid native turn amount");
            float delta=float(amount)*.01745329238474369f;
            if(callback==2)delta=-delta;
            body.pose.yaw=edactormath::normalizeAngle(float(body.pose.yaw+delta));
        }
    }
    void rootMotion(Body& body,const std::array<edactions::Playback,50>& before) {
        int index=body.actions.layers[0].playback;
        for(unsigned layer=1;layer<7;++layer) {
            const auto& candidate=body.actions.layers[layer];
            if(candidate.state>=0&&candidate.playback>=0) {
                const auto& mask=body.actions.states.at(size_t(candidate.state)).boneMask;
                if(!mask.empty()&&mask[0]){index=candidate.playback;break;}
            }
        }
        if(index<0)return;
        if(body.actions.playbacks[size_t(index)].mode==4&&body.actions.playbacks[size_t(index)].parent>=0)
            index=body.actions.playbacks[size_t(index)].parent;
        const auto& current=body.actions.playbacks[size_t(index)];const auto& previous=before[size_t(index)];
        if(!current.active||current.state!=previous.state)return;const auto& state=body.actions.states.at(size_t(current.state));
        if(previous.frame<0||unsigned(previous.frame)>=state.frames)return;
        // DC8c061b20 resets both frame registers when a deferred stop skips to its tail.
        if(previous.mode==6&&current.mode==3&&previous.frame+previous.step>state.earlyEnd)return;
        if((state.clipFlags&1)&&current.frame<=previous.frame)return;
        if(host.rootMotionAllowed&&!host.rootMotionAllowed(body.key,state))return;
        const auto& mesh=asset(body).mesh;
        const auto a=mesh.rootPosition(state.animation,edactions::curveFrame(state,previous));
        const auto b=mesh.rootPosition(state.animation,edactions::curveFrame(state,current));
        body.pendingMove[0]+=a[0]-b[0];body.pendingMove[2]+=b[2]-a[2]; // Native401a4e leaves rotation to4134f0 physics.
    }
    int createNative(int type,int mesh,int skeleton,int texture) {
        auto& source=asset(uint32_t(mesh));const auto& model=*source.definition;const int capacity=model.capacity;
        if(type!=model.type||uint32_t(skeleton)!=model.skeleton||uint32_t(texture)!=model.texture)
            throw jpc::Error("Unsupported original actor resource combination");
        for(int index=0;index<capacity;++index)if(!actors.count({type,index})) {
            const Key key{type,index};const auto pose=required(host.spawnPose,"spawnPose")(key,mesh,skeleton,texture);valid(pose);
            int32_t floorTriangle=-1;const float floor=host.floorHeightForActor?0:actorFloor(key,pose.x,pose.z,floorTriangle);
            const float localY=pose.y-floor;
            if(!std::isfinite(localY))throw jpc::Error("Non-finite native actor local height");
            const uint32_t order=uint32_t(actors.size());
            std::array<bool,13> occupied{};for(const auto& [other,b]:actors){(void)other;if(b.nativeSlot>=0)occupied[size_t(b.nativeSlot)]=true;}
            int slot=-1;for(int n=0;n<13;++n)if(!occupied[size_t(n)]){slot=n;break;}
            auto [at,inserted]=actors.try_emplace(key,key,pose,source.actions,source.mesh,model,poolHooks(key));(void)inserted;at->second.localY=localY;at->second.floorTriangle=floorTriangle;at->second.cachedFloorHeight=floor;at->second.allocationOrder=order;at->second.nativeSlot=slot;compose(at->second);return index;
        }
        return -1;
    }
    void bind(const char* name,jpc::Interpreter::Native implementation) {
        const jpc::Method* match=nullptr;
        for(const auto& method:game.package.methods)if(game.package.classes[size_t(method.owner)].name=="ed/base/External"&&method.name==name) {
            if(match)throw jpc::Error("Ambiguous actor native binding");match=&method;
        }
        if(!match)throw jpc::Error(std::string("Missing original actor native: ")+name);
        game.vm.bind("ed/base/External",name,match->signature,std::move(implementation));
    }
    int effect(const std::string& name,const std::vector<int>& args) {return required(host.effect,name.c_str())(name,args);}
    bool queue(Message message) {
        // Native40d250/40d310 deduplicate this key and retain the first payload.
        for(const auto& pending:messages)if(pending.first==message.first&&pending.second==message.second&&pending.event==message.event)return false;
        if(messages.size()>=50)throw jpc::Error("Native actor message queue capacity exceeded");messages.push_back(message);
        return true;
    }
    void dispatch(Key listener,const Message& message) {
        const bool reserved=reservedPickupMessage(listener,message);
        if(reserved&&(pickup.receiptPhase==2||!pickupMatches(pickup.receipt))){clearPickupReceipt();return;}
        auto& body=at(listener);if(!body.script||(body.flags&8)){if(reserved)clearPickupReceipt();return;}
        if(listener.type==0&&(message.event>=36&&message.event<=42)&&host.playerDamageAllowed&&!host.playerDamageAllowed())return;
        const auto id=game.vm.start(game.package.methodIndex("ActorDB","processMessage","(IIIIIIIIIII)I"),0,
            {I(listener.type),I(listener.index),I(message.first.type),I(message.first.index),I(message.second.type),I(message.second.index),
             I(message.event),I(message.data[0]),I(message.data[1]),I(message.data[2]),I(message.data[3])});
        game.vm.notify(message.first.type,message.first.index,message.event);callbacks.push_back({listener,message,id});
    }
    void collect() {
        std::vector<Dispatch> waiting;
        for(const auto& callback:callbacks) {
            if(game.vm.status(callback.thread)!=jpc::Interpreter::ThreadStatus::Complete){waiting.push_back(callback);continue;}
            if(game.vm.result(callback.thread).integer()==0)waiting.push_back({callback.listener,callback.message,0});
            else if(reservedPickupMessage(callback.listener,callback.message))clearPickupReceipt();
            game.vm.forget(callback.thread);
        }
        callbacks=std::move(waiting);
    }
    void guiPrerequisites() {
        // Existing ScriptGame owns GUI bindings. These source wrapper objects are
        // the only additional references required by full Ash.initialize659.
        const auto ensure=[&](uint32_t field,const char* owner) {
            if(!game.vm.staticField(field).null())return;
            const auto ref=game.vm.makeObject(game.package.classIndex(owner),{{835,R(game.vm.makeArray('I',{I(101),I(102)}))},{836,I(201)}});
            game.vm.statics[field]=R(ref);
        };
        ensure(870,"InvFuel");ensure(869,"InvFace");
    }
public:
    void clearPickup() noexcept {pickup={};}
    void cancelPickup() noexcept {pickup.query={};if(!pickup.queried)pickup.queryTick=0;pickup.target={};if(pickup.receiptPhase)pickup.receiptPhase=2;}
    void invalidatePickup(int slot) {
        if(slot<0||slot>=32)throw jpc::Error("Pickup invalidation slot outside source pool");
        if(pickup.query.index==slot)pickup.query={};
        if(pickup.target.index==slot)pickup.target={}; // Keep its playback tombstone; never select a replacement at contact.
        if(pickup.receipt.index==slot&&pickup.receiptPhase)pickup.receiptPhase=2;
    }
    const std::set<uint32_t>& textureCatalog() const {return textureIDs;}
    const std::set<int>& supportedTypes() const {return loadedTypes;}
    GameplayActors(ScriptGame& scripts,const std::filesystem::path& root,Host callbacks,bool sharedPlaybacks=true)
        :game(scripts),host(std::move(callbacks)),globalPlayback(sharedPlaybacks) {
        levelB=std::any_of(game.package.classes.begin(),game.package.classes.end(),[](const auto& c){return c.name=="LevelB";});
        for(const auto& model:Models)if(model.levels&(levelB?2:1)) {const auto* name=model.name;
            Asset source{&model,edactions::load(root/"actors"/name/"actions.bin"),edmesh::ActorMesh::load(root/"actors"/name/"actor-parts.bin"),{}};
            loadAnimationEvents(source,root/"actors"/name/"animation-events.bin",edanimsound::verifiedActor(model.type));
            for(const auto& state:source.actions)if(state.animation) {
                const auto& clip=source.mesh.clip(state.animation);
                if(clip.frameCount!=state.frames||source.mesh.bones.size()!=state.bones)throw jpc::Error("Actor action/mesh source mismatch");
            }
            if(!assets.emplace(model.mesh,std::move(source)).second)throw jpc::Error("Duplicate original actor mesh");loadedTypes.insert(model.type);
        }
        loadTextures(root);
        bind("actorCreate",[this](auto&,auto,const auto& args){return I(createNative(args[0].integer(),args[1].integer(),args[2].integer(),args[3].integer()));});
        bind("registerActorMessageInterest",[this](auto&,auto,const auto& args){
            const int type=args[0].integer(),index=args[1].integer(),firstType=args[2].integer(),firstIndex=args[3].integer(),event=args[6].integer();
            if(index<0||index>=actorCapacity(type))throw jpc::Error("Native actor interest index outside source capacity");
            for(auto& interest:interests)if(std::tie(interest.listenerType,interest.firstType,interest.firstIndex,interest.event)==std::tie(type,firstType,firstIndex,event)) {
                interest.indices.insert(index);return jpc::Value{};
            }
            if(interests.size()>=256)throw jpc::Error("Excessive actor message interests");interests.push_back({type,firstType,firstIndex,event,{index}});return jpc::Value{};
        });
        for(const auto* name:{"actorFlagsSet","actorFlagsAdd","actorFlagsSub","actorFlagsGet","actorGetFlags","actorSetVisible",
                "actorSetBRadius","actorSetAttrFOV","actorSetAttrSight","actorDoConsoleAction","stateTransitionIsPossible",
                "actorSetPositionFloorHeight","actorSetPartVisible","actorGetPositionX","actorGetPositionY","actorGetPositionZ"}) {
            bind(name,[this,name=std::string(name)](auto&,auto,const auto& values){
                std::vector<int> a;for(auto value:values)a.push_back(value.integer());auto& body=at({a[0],a[1]});
                if(name=="actorFlagsSet")body.flags=a[2];
                else if(name=="actorFlagsAdd")body.flags|=a[2];
                else if(name=="actorFlagsSub") {if(body.flags&a[2])body.flags^=a[2];}
                if((name=="actorFlagsAdd"||name=="actorFlagsSub")&&(a[2]&0x40))effect("scrFxLetterbox",{name=="actorFlagsAdd"?1:0});
                if(name=="actorFlagsSet"||name=="actorFlagsAdd"||name=="actorFlagsSub"||name=="actorFlagsGet"||name=="actorGetFlags")return I(body.flags);
                if(name=="actorSetVisible")body.flags=a[2]?body.flags|1:body.flags&~1;
                else if(name=="actorSetBRadius")body.radius=a[2];
                else if(name=="actorSetAttrFOV")body.fov=a[2];
                else if(name=="actorSetAttrSight")body.sight=a[2];
                else if(name=="actorDoConsoleAction")return I(command(body,a[2])?1:0);
                else if(name=="stateTransitionIsPossible")return I(body.actions.possible(a[2])?1:0);
                else if(name=="actorSetPositionFloorHeight")setFloorHeight(body,a[2]);
                else if(name=="actorSetPartVisible") {
                    if(a[4])std::fill(body.parts.begin(),body.parts.end(),false);
                    if(a[2]==-1)std::fill(body.parts.begin(),body.parts.end(),a[3]!=0);
                    else {if(a[2]<0||size_t(a[2])>=body.parts.size())throw jpc::Error("Actor part outside source mesh");body.parts[size_t(a[2])]=a[3]!=0;}
                    body.vertices=asset(body).mesh.skinPose(body.localPose,body.parts);
                }
                else if(name=="actorGetPositionX")return I(fixed(body.pose.x));
                else if(name=="actorGetPositionY")return I(fixed(body.localY));
                else if(name=="actorGetPositionZ")return I(fixed(body.pose.z));
                valid(body.pose);return jpc::Value{};
            });
        }
        bind("sendActorMessage",[this](auto&,auto,const auto& values){std::vector<int> a;for(auto value:values)a.push_back(value.integer());
            queue({{a[0],a[1]},{a[3],a[4]},a[2],{{a[5],a[6],a[7],0}}});return jpc::Value{};});
        bind("random",[this](auto&,auto,const auto& args){const int count=args[0].integer();if(!count)return I(0);
            const int value=required(host.random,"random")(count);if(value<0||int64_t(value)>=std::abs(int64_t(count)))throw jpc::Error("Actor random result outside source range");return I(value);});
        bind("sceneIsNPCInVisArea",[this](auto&,auto,const auto& args){return I(required(host.visibleArea,"visibleArea")({args[0].integer(),args[1].integer()})?1:0);});
        bind("actorHaveLOSToActor",[this](auto&,auto,const auto& args){return I(required(host.lineOfSight,"lineOfSight")({args[0].integer(),args[1].integer()},{args[2].integer(),args[3].integer()})?1:0);});
        bind("actorHaveLOSToDir",[this](auto&,auto,const auto& args){return I(required(host.lineOfSightDirection,"lineOfSightDirection")({args[0].integer(),args[1].integer()},args[2].integer(),args[3].integer())?1:0);});
        bind("actorIsFacingActor",[this](auto&,auto,const auto& args){return I(required(host.facing,"facing")({args[0].integer(),args[1].integer()},{args[2].integer(),args[3].integer()},args[4].integer())?1:0);});
        bind("actorGetPickupObject",[this](auto&,auto,const auto& args){
            const Key key{args[0].integer(),args[1].integer()};const auto objects=pickupCandidates(at(key));const int ordinal=args[2].integer();
            const int result=ordinal>=0&&size_t(ordinal)<objects.size()?objects[size_t(ordinal)]:-1;
            if(game.profile.dreamcast&&key==Key{0,0}) {
                pickup.query={};pickup.queryTick=0;pickup.queried=false;
                if(result>=0)for(const auto& object:required(host.pickupObjects,"pickupObjects")())if(object.index==result){pickup.query=pickupTarget(object);pickup.queryTick=sourceTick;pickup.queried=true;break;}
            }
            return I(result); // Native404d30 eligible ordinal, not a raw pool index.
        });
        bind("actorFaceActor",[this](auto&,auto,const auto& args){auto& body=at({args[0].integer(),args[1].integer()});const auto& other=at({args[2].integer(),args[3].integer()});
            body.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(other.pose.z)-body.pose.z,double(other.pose.x)-body.pose.x)));return jpc::Value{};});
        bind("actorTurn",[this](auto&,auto,const auto& args){auto& body=at({args[0].integer(),args[1].integer()});
            // Native405330 stores the degree delta tofloat;401b40 adds it to
            // MODEL EulerY, stores again, then421760 normalizes that float.
            const float delta=float(double(args[2].integer())*.01745329238474369f);
            body.pose.yaw=edactormath::normalizeAngle(float(double(edactormath::normalizeAngle(body.pose.yaw))+delta));return jpc::Value{};
        });
        bind("actorGetAngle",[this](auto&,auto,const auto& args){
            constexpr float degrees=57.295780181884765625f;return I(int32_t(double(edactormath::nativeHeading(at({args[0].integer(),args[1].integer()}).pose.yaw))*degrees));
        });
        bind("actorGetRotY",[this](auto&,auto,const auto& args){
            // Native403540 reads MODEL Euler+94, unlike actorGetAngle's heading+8C.
            constexpr float degrees=57.295780181884765625f;
            return I(int32_t(double(edactormath::normalizeAngle(at({args[0].integer(),args[1].integer()}).pose.yaw))*degrees));
        });
        bind("actorSetTexture",[this](auto&,auto,const auto& args){
            auto& body=at({args[0].integer(),args[1].integer()});
            // Native4038c0 resolves resource IDs through4190c0 (top three bits ignored),
            // then431690 replaces both wrapper/model texture pointers.
            const uint32_t id=uint32_t(args[2].integer())&0x1fffffff;
            if(!textureIDs.count(id))
                throw jpc::Error("Unexported original actor texture variant");
            body.textureID=id;return jpc::Value{};
        });
        bind("actorGetRelativeAngle",[this](auto&,auto,const auto& args){
            const auto& a=at({args[0].integer(),args[1].integer()}).pose;const auto& b=at({args[2].integer(),args[3].integer()}).pose;
            constexpr double pi=3.14159265358979323846;double heading=std::atan2(double(b.z)-a.z,double(b.x)-a.x);
            if(heading<0)heading+=2*pi;return I(int32_t(heading*180/pi)); // Native404c60 returns absolute target bearing.
        });
        for(const auto* name:{"actorDistanceToActor","actorDistanceToActor3D"})bind(name,[this,name=std::string(name)](auto&,auto,const auto& args){
            const auto& a=at({args[0].integer(),args[1].integer()});const auto& b=at({args[2].integer(),args[3].integer()});
            const double x=double(a.pose.x)-b.pose.x,z=double(a.pose.z)-b.pose.z,y=name=="actorDistanceToActor3D"?double(a.localY)-b.localY:0;
            const double squared=x*x+y*y+z*z;double distance=(squared==0?1:std::sqrt(squared))*100;
            // Native403960/403a10 subtract only target radius, and only outside it.
            if(distance>b.radius)distance-=b.radius;
            if(distance>std::numeric_limits<int32_t>::max())throw jpc::Error("Actor distance exceeds native range");return I(int32_t(distance));
        });
        for(const auto* name:{"actorMoveForward","actorMoveStrafe","actorMoveVertical"})bind(name,[this,name=std::string(name)](auto&,auto,const auto& args){
            auto& body=at({args[0].integer(),args[1].integer()});const int amount=args[2].integer();
            const int result=required(host.move,"move")(body,name=="actorMoveForward"?amount:0,name=="actorMoveStrafe"?amount:0,name=="actorMoveVertical"?amount:0);valid(body.pose);
            for(float delta:body.pendingMove)if(!std::isfinite(delta))throw jpc::Error("Non-finite pending actor movement");
            if(result!=1)throw jpc::Error("Native actor movement must return constant1");return I(result);
        });
        for(const auto* name:{"actorEffectFadeInfo","actorEffectFadeStart","vfxSpawnActorJoint","vfxSpawnActorOffset"})bind(name,[this,name=std::string(name)](auto&,auto,const auto& args){
            std::vector<int> values;for(auto value:args)values.push_back(value.integer());effect(name,values);return jpc::Value{};
        });
        guiPrerequisites();
    }
    GameplayActors(const GameplayActors&)=delete;
    GameplayActors& operator=(const GameplayActors&)=delete;
    Body& at(Key key) {auto found=actors.find(key);if(found==actors.end())throw jpc::Error("Unknown native actor");return found->second;}
    const Body& at(Key key) const {auto found=actors.find(key);if(found==actors.end())throw jpc::Error("Unknown native actor");return found->second;}
    const std::map<Key,Body>& bodies() const {return actors;}
    std::array<float,3> jointWorld(Key key,unsigned joint,const std::array<int32_t,3>& offsetCm={{0,0,0}}) const {
        const auto& body=at(key);const auto raw=assets.at(body.meshResource).mesh.jointPosition(body.localPose,joint);
        return edactormath::jointWorld({{0,body.pose.yaw,0}},raw,{{body.pose.x,body.pose.y,body.pose.z}},offsetCm);
    }

    void prepareBootstrap(){guiPrerequisites();}
    void unregister(Key key){at(key).nativeSlot=-1;} // Native402e80 only unregisters; it does not destroy actor data.
    void captureLevelBoundary() {
        // Native40cbdf/40cfe0 saves the arena cursor immediately after the real
        // Level.create call. Allocation order represents that memory prefix.
        if(boundaryCaptured||actors.empty())throw jpc::Error("Invalid source level allocation boundary capture");
        std::set<uint32_t> orders;for(const auto& [key,body]:actors){(void)key;orders.insert(body.allocationOrder);}
        if(orders.size()!=actors.size()||*orders.begin()!=0||*orders.rbegin()+1!=actors.size())throw jpc::Error("Noncontiguous source level allocations");
        levelBoundary=uint32_t(actors.size());boundaryCaptured=true;
    }
    std::vector<Key> unloadScene() {
        if(!boundaryCaptured)throw jpc::Error("Source level allocation boundary was not captured");
        cancelPickup();pickup.query={};pickup.queryTick=0;pickup.queried=false;
        if(globalPlayback)playbackPool.clearExceptType(0); //4055c0 keeps nativeP1type0, independent physical13.
        std::vector<Key> removed;for(const auto& [key,body]:actors)if(body.allocationOrder>=levelBoundary)removed.push_back(key);
        for(auto key:removed)actors.erase(key); // Native4167f5 dataPtr>=savedCursor, then wrapper+4=0.
        for(auto& [key,body]:actors)if(key.type!=0) {
            body.actions=edactions::ActorActions(asset(body).actions,poolHooks(key),false);
        }
        interests.erase(std::remove_if(interests.begin(),interests.end(),[](const auto& interest){return interest.listenerType!=0;}),interests.end());
        messages.clear();callbacks.clear();clearPickupReceipt();return removed;
    }
    void resetSceneRuntime() {
        if(!boundaryCaptured)throw jpc::Error("Source level allocation boundary was not captured");
        cancelPickup();pickup.query={};pickup.queryTick=0;pickup.queried=false;
        // Native418560 occurs AFTER native scene/route load and BEFORE the
        // original Main.createScene824/LevelA1783 re-registers NPC interests.
        // Fresh package bootstrap has resident NPC idle allocations too; this
        // source4055c0 clear is required evenwhen unloadScene wasnotcalled.
        if(globalPlayback)playbackPool.clearExceptType(0);
        for(auto& [key,body]:actors)if(key.type!=0){body.actions=edactions::ActorActions(asset(body).actions,poolHooks(key));compose(body);}
        game.vm.clearSceneThreads();for(auto& [key,body]:actors){(void)key;body.think=0;}
        sync();
    }
    State cloneState() const {
        State result;result.globalPlayback=globalPlayback;result.playbackPool=playbackPool.state();result.animationHeight=animationHeight;result.pickup=pickup;result.interests=interests;result.messages=messages;result.callbacks=callbacks;result.sourceTick=sourceTick;result.updated=updated;result.levelBoundary=levelBoundary;result.boundaryCaptured=boundaryCaptured;
        for(const auto& [key,body]:actors)result.bodies.push_back({key,script(key),body.pose,body.localY,body.pendingMove,body.flags,body.radius,body.fov,body.sight,body.parts,
            body.actions.layers,body.actions.playbacks,body.actions.events,body.actions.updates,body.localPose,body.think,body.allocationOrder,body.motionFlags,body.motionFlags2,body.floorTriangle,body.cachedFloorHeight,body.textureID,body.nativeSlot,body.sfxMaterial,body.meshResource});
        return result;
    }
    std::vector<uint8_t> snapshot() const {
        const auto state=cloneState();statebytes::Writer out;out.text("EDACTORS13");
        auto key=[&](Key k){out.put<int32_t>(k.type);out.put<int32_t>(k.index);};
        auto message=[&](const Message& m){key(m.first);key(m.second);out.put<int32_t>(m.event);for(int n:m.data)out.put<int32_t>(n);};
        out.flag(state.globalPlayback);for(const auto& slot:state.playbackPool){key({slot.owner.type,slot.owner.index});out.flag(slot.active);out.flag(slot.reusable);}
        out.put<uint64_t>(state.sourceTick);out.flag(state.updated);out.flag(state.boundaryCaptured);out.put<uint32_t>(state.levelBoundary);out.put<uint32_t>(uint32_t(state.bodies.size()));
        for(const auto& b:state.bodies) {
            key(b.key);out.put<uint32_t>(b.meshResource);out.put<jpc::Ref>(b.script);for(float n:{b.pose.x,b.pose.y,b.pose.z,b.pose.yaw,b.localY})out.put<float>(n);
            for(float n:b.pendingMove)out.put<float>(n);
            for(int n:{b.flags,b.radius,b.fov,b.sight})out.put<int32_t>(n);out.flags(b.parts);
            for(const auto& layer:b.layers) {out.put<int32_t>(layer.state);out.put<int32_t>(layer.playback);out.put<int32_t>(layer.historyIndex);for(int n:layer.history)out.put<int32_t>(n);}
            for(const auto& p:b.playbacks) {
                for(int n:{p.state,p.frame,p.mode,p.desiredMode,p.step,p.parent,p.child})out.put<int32_t>(n);out.flag(p.active);out.flag(p.reusable);
            }
            out.put<uint32_t>(uint32_t(b.events.size()));for(const auto& event:b.events) {
                out.put<uint64_t>(event.update);out.put<uint8_t>(event.event);out.put<uint32_t>(event.playback);out.put<uint32_t>(event.state);
            }
            out.put<uint64_t>(b.updates);out.put<float>(b.localPose.rootY);out.put<uint32_t>(uint32_t(b.localPose.rotationsDegrees.size()));
            for(const auto& rotation:b.localPose.rotationsDegrees)for(float n:rotation)out.put<float>(n);out.put<jpc::Interpreter::ThreadID>(b.think);out.put<uint32_t>(b.allocationOrder);out.put<uint8_t>(b.motionFlags);out.put<uint8_t>(b.motionFlags2);out.put<int32_t>(b.floorTriangle);out.put<float>(b.cachedFloorHeight);out.put<uint32_t>(b.textureID);out.put<int32_t>(b.nativeSlot);
            for(uint8_t code:b.sfxMaterial)out.put<uint8_t>(code);
        }
        out.put<uint32_t>(uint32_t(state.interests.size()));for(const auto& i:state.interests) {
            for(int n:{i.listenerType,i.firstType,i.firstIndex,i.event})out.put<int32_t>(n);
            out.put<uint32_t>(uint32_t(i.indices.size()));for(int n:i.indices)out.put<int32_t>(n);
        }
        out.put<uint32_t>(uint32_t(state.messages.size()));for(const auto& m:state.messages)message(m);
        out.put<uint32_t>(uint32_t(state.callbacks.size()));for(const auto& callback:state.callbacks) {
            key(callback.listener);message(callback.message);out.put<jpc::Interpreter::ThreadID>(callback.thread);
        }
        key({state.animationHeight.owner.type,state.animationHeight.owner.index});out.put<float>(state.animationHeight.value);out.flag(state.animationHeight.pending);
        const auto target=[&](const PickupTarget& value){out.put<int32_t>(value.index);for(int n:value.setup)out.put<int32_t>(n);for(float n:value.position)out.put<float>(n);};
        target(state.pickup.query);target(state.pickup.target);out.put<uint64_t>(state.pickup.queryTick);out.put<int32_t>(state.pickup.playback);out.put<int32_t>(state.pickup.state);out.flag(state.pickup.queried);
        target(state.pickup.receipt);out.put<uint8_t>(state.pickup.receiptPhase);
        if(out.bytes.size()>4*1024*1024)throw jpc::Error("Excessive actor registry snapshot");return out.bytes;
    }
    void restore(const std::vector<uint8_t>& bytes) {
        if(bytes.size()>4*1024*1024)throw jpc::Error("Excessive actor registry snapshot");statebytes::Reader input{bytes};
        const auto magic=input.text();int version=0;
        if(magic=="EDACTORS13")version=13;
        else if(magic=="EDACTORS12")version=12;
        else if(magic=="EDACTORS11")version=11;
        else if(magic=="EDACTORS10")version=10;
        else if(magic.size()==9&&magic.substr(0,8)=="EDACTORS"&&magic[8]>='1'&&magic[8]<='9')version=magic[8]-'0';
        else throw jpc::Error("Invalid actor registry snapshot header");
        auto key=[&](){const int type=input.get<int32_t>(),index=input.get<int32_t>();return Key{type,index};};
        auto message=[&](){Message m;m.first=key();m.second=key();m.event=input.get<int32_t>();for(auto& n:m.data)n=input.get<int32_t>();return m;};
        State state;state.globalPlayback=false;
        if(version>=11){state.globalPlayback=input.flag();for(auto& slot:state.playbackPool){const auto k=key();slot.owner={k.type,k.index};slot.active=input.flag();slot.reusable=input.flag();}}
        // Old per-body pool snapshots do not encode historical global slot
        // ownership. Reject rather than silently invent cross-actor ordering.
        if(state.globalPlayback!=globalPlayback)throw jpc::Error("Saved playback allocation mode/ownership is unavailable");
        state.sourceTick=input.get<uint64_t>();state.updated=input.flag();if(version>=7){state.boundaryCaptured=input.flag();state.levelBoundary=input.get<uint32_t>();}const auto bodies=input.count(MaxBodies);
        for(uint32_t n=0;n<bodies;++n) {
            BodyState b;b.key=key();b.meshResource=version>=10?input.get<uint32_t>():defaultModel(b.key.type).mesh;
            const auto& source=asset(b.meshResource);b.script=input.get<jpc::Ref>();
            b.pose={input.get<float>(),input.get<float>(),input.get<float>(),input.get<float>()};
            b.localY=input.get<float>();
            for(auto& delta:b.pendingMove)delta=input.get<float>();
            b.flags=input.get<int32_t>();b.radius=input.get<int32_t>();b.fov=input.get<int32_t>();b.sight=input.get<int32_t>();b.parts=input.flags(source.mesh.partCount);
            for(auto& layer:b.layers) {layer.state=input.get<int32_t>();layer.playback=input.get<int32_t>();layer.historyIndex=input.get<int32_t>();for(auto& i:layer.history)i=input.get<int32_t>();}
            for(auto& p:b.playbacks) {
                p.state=input.get<int32_t>();p.frame=input.get<int32_t>();p.mode=input.get<int32_t>();p.desiredMode=input.get<int32_t>();p.step=input.get<int32_t>();
                p.parent=input.get<int32_t>();p.child=input.get<int32_t>();p.active=input.flag();p.reusable=input.flag();
            }
            const auto events=input.count(1000);for(uint32_t i=0;i<events;++i)b.events.push_back({input.get<uint64_t>(),input.get<uint8_t>(),input.get<uint32_t>(),input.get<uint32_t>()});
            b.updates=input.get<uint64_t>();b.localPose.rootY=input.get<float>();const auto bones=input.count(256);
            if(bones!=source.mesh.bones.size())throw jpc::Error("Saved local pose does not match actor skeleton");
            for(uint32_t i=0;i<bones;++i)b.localPose.rotationsDegrees.push_back({input.get<float>(),input.get<float>(),input.get<float>()});
            b.think=input.get<jpc::Interpreter::ThreadID>();
            b.allocationOrder=magic!="EDACTORS1"?input.get<uint32_t>():uint32_t(b.key.type==0?0:b.key.type==4?b.key.index+1:b.key.index+5);
            if(version>=3){b.motionFlags=input.get<uint8_t>();b.motionFlags2=input.get<uint8_t>();}
            else { // Previous prototype stored world pending vectors; migrate to native default local mode.
                const float x=b.pendingMove[0],z=b.pendingMove[2],c=std::cos(b.pose.yaw),s=std::sin(b.pose.yaw);
                b.pendingMove[0]=x*c-z*s;b.pendingMove[2]=z*c+x*s;
            }
            if(version>=4)b.floorTriangle=input.get<int32_t>();
            b.cachedFloorHeight=version>=5?input.get<float>():b.pose.y-b.localY;
            b.textureID=version>=6?input.get<uint32_t>():defaultTexture(b.key.type);
            b.nativeSlot=version>=8?input.get<int32_t>():b.allocationOrder<13?int32_t(b.allocationOrder):-1;
            if(version>=9)for(auto& code:b.sfxMaterial)code=input.get<uint8_t>();
            state.bodies.push_back(std::move(b));
        }
        if(version<7) {
            // Old full-LevelA saves have the actual1778 prefix: Ash,4Deadite,
            // 4SkullBat. Smaller CPU fixtures have no captured arena boundary.
            std::set<uint32_t> resident;for(const auto& b:state.bodies)if(b.key.type==0||b.key.type==2||b.key.type==4)resident.insert(b.allocationOrder);
            if(resident.size()==9&&*resident.begin()==0&&*resident.rbegin()==8){state.boundaryCaptured=true;state.levelBoundary=9;}
        }
        const auto interestCount=input.count(256);for(uint32_t n=0;n<interestCount;++n) {
            Interest i;i.listenerType=input.get<int32_t>();i.firstType=input.get<int32_t>();i.firstIndex=input.get<int32_t>();i.event=input.get<int32_t>();
            const auto indices=input.count(4);for(uint32_t j=0;j<indices;++j)if(!i.indices.insert(input.get<int32_t>()).second)throw jpc::Error("Duplicate saved interest index");
            state.interests.push_back(std::move(i));
        }
        const auto messageCount=input.count(50);for(uint32_t n=0;n<messageCount;++n)state.messages.push_back(message());
        const auto callbackCount=input.count(1024);for(uint32_t n=0;n<callbackCount;++n) {
            Dispatch d;d.listener=key();d.message=message();d.thread=input.get<jpc::Interpreter::ThreadID>();state.callbacks.push_back(d);
        }
        if(version>=12){const auto owner=key();state.animationHeight.owner={owner.type,owner.index};state.animationHeight.value=input.get<float>();state.animationHeight.pending=input.flag();}
        if(version>=13){const auto target=[&](){PickupTarget value;value.index=input.get<int32_t>();for(auto& n:value.setup)n=input.get<int32_t>();for(auto& n:value.position)n=input.get<float>();return value;};
            state.pickup.query=target();state.pickup.target=target();state.pickup.queryTick=input.get<uint64_t>();state.pickup.playback=input.get<int32_t>();state.pickup.state=input.get<int32_t>();state.pickup.queried=input.flag();}
        if(version>=13){state.pickup.receipt.index=input.get<int32_t>();for(auto& n:state.pickup.receipt.setup)n=input.get<int32_t>();for(auto& n:state.pickup.receipt.position)n=input.get<float>();state.pickup.receiptPhase=input.get<uint8_t>();}
        input.end();restore(state);
    }
    // Restore the paired VM snapshot first; validation/skin construction commit no
    // actor state until every numeric ref, playback and thread has been checked.
    void restore(const State& state) {
        if(state.globalPlayback!=globalPlayback)throw jpc::Error("Saved playback allocation mode mismatch");
        if(state.bodies.size()>MaxBodies||state.interests.size()>256||state.messages.size()>50||state.callbacks.size()>1024||
           state.updated!=(state.sourceTick!=0)||state.levelBoundary>MaxBodies||state.boundaryCaptured!=(state.levelBoundary!=0))throw jpc::Error("Invalid actor registry state counts");
        const auto& height=state.animationHeight;height.centimeters(game.profile.dreamcast);
        if(height.owner.empty()) {
            if(height.owner!=edactions::PlaybackOwner{}||height.pending||height.value!=0)throw jpc::Error("Invalid deferred animation height owner");
        } else if(height.owner.index<0||height.owner.index>=defaultModel(height.owner.type).capacity)
            throw jpc::Error("Invalid deferred animation height owner");
        std::map<Key,Body> candidate;std::set<uint32_t> allocationOrders;std::set<int32_t> nativeSlots;
        for(const auto& saved:state.bodies) {
            valid(saved.pose);if(!std::isfinite(saved.localY)||!std::isfinite(saved.cachedFloorHeight))throw jpc::Error("Non-finite native actor local height");
            if(host.floorHeightForActor&&std::abs(double(saved.pose.y)-(double(saved.localY)+saved.cachedFloorHeight))>
                1e-6*std::max(1.0,std::abs(double(saved.pose.y))))throw jpc::Error("Saved actor cached floor/world height mismatch");
            for(float delta:saved.pendingMove)if(!std::isfinite(delta))throw jpc::Error("Non-finite pending actor movement");
            auto& source=asset(saved.meshResource);const int capacity=source.definition->capacity;
            if(!textureIDs.count(saved.textureID))throw jpc::Error("Saved actor texture is not exported");
            if(saved.key.type!=source.definition->type||saved.key.index<0||saved.key.index>=capacity||!saved.script||script(saved.key)!=saved.script||
               game.vm.field(saved.script,6).integer()!=saved.key.type||game.vm.field(saved.script,5).integer()!=saved.key.index||
               game.vm.objectClass(saved.script)!=game.package.classIndex(source.definition->scriptClass)||
               saved.radius<0||saved.floorTriangle< -1||saved.floorTriangle>=100000||saved.parts.size()!=source.mesh.partCount||saved.events.size()>1000)
                throw jpc::Error("Invalid saved native actor identity");
            if(saved.allocationOrder>=MaxBodies||!allocationOrders.insert(saved.allocationOrder).second)throw jpc::Error("Invalid saved native allocation order");
            if(saved.nativeSlot< -1||saved.nativeSlot>=13||(saved.nativeSlot>=0&&!nativeSlots.insert(saved.nativeSlot).second))throw jpc::Error("Invalid saved physical actor slot");
            if(saved.key.type==0&&game.vm.objectClass(game.vm.field(saved.script,99).reference())!=game.package.classIndex("Inventory"))
                throw jpc::Error("Saved actor inventory reference is invalid");
            const auto stateIndex=[&](int n){return n>=-1&&(n<0||size_t(n)<source.actions.size());};
            const auto playbackIndex=[](int n){return n>=-1&&n<50;};
            for(const auto& layer:saved.layers) {
                if(!stateIndex(layer.state)||!playbackIndex(layer.playback)||layer.historyIndex<0||layer.historyIndex>=10)
                    throw jpc::Error("Invalid saved actor layer");
                for(int n:layer.history)if(!stateIndex(n))throw jpc::Error("Invalid saved actor history");
            }
            for(unsigned index=0;index<saved.playbacks.size();++index) {
                const auto& p=saved.playbacks[index];
                if(p.state<0||size_t(p.state)>=source.actions.size()||p.mode<0||p.mode>6||p.desiredMode<0||p.desiredMode>2||
                   (p.step!=-1&&p.step!=1)||!playbackIndex(p.parent)||!playbackIndex(p.child)||p.parent==int(index)||p.child==int(index)||
                   (p.active&&(p.frame<0||unsigned(p.frame)>=source.actions[size_t(p.state)].frames||p.reusable!=globalPlayback)))
                    throw jpc::Error("Invalid saved actor playback");
            }
            for(const auto& event:saved.events)if(event.playback>=50||event.state>=source.actions.size()||event.update>saved.updates)
                throw jpc::Error("Invalid saved actor event");
            if(saved.think) {
                if(!game.vm.threadMatches(saved.think,1))throw jpc::Error("Saved actor think thread flags or ownership mismatch");
                if(game.vm.field(saved.script,10).integer()!=int32_t(saved.think))throw jpc::Error("Saved actor think thread does not match source ActorDB");
            }
            auto [at,inserted]=candidate.try_emplace(saved.key,saved.key,saved.pose,source.actions,source.mesh,*source.definition,edactions::PoolHooks{},false);
            if(!inserted)throw jpc::Error("Duplicate saved actor key");auto& body=at->second;
            body.script=saved.script;body.localY=saved.localY;body.pendingMove=saved.pendingMove;body.flags=saved.flags;body.radius=saved.radius;body.fov=saved.fov;body.sight=saved.sight;body.parts=saved.parts;body.textureID=saved.textureID;body.nativeSlot=saved.nativeSlot;body.sfxMaterial=saved.sfxMaterial;
            body.actions.layers=saved.layers;body.actions.playbacks=saved.playbacks;body.actions.events=saved.events;body.actions.updates=saved.updates;
            body.localPose=saved.localPose;body.think=saved.think;body.allocationOrder=saved.allocationOrder;body.motionFlags=saved.motionFlags;body.motionFlags2=saved.motionFlags2;body.floorTriangle=saved.floorTriangle;body.cachedFloorHeight=saved.cachedFloorHeight;body.vertices=source.mesh.skinPose(body.localPose,body.parts);
        }
        edactions::SourcePlaybackPool nextPool;nextPool.restore(state.playbackPool);
        if(globalPlayback){
            for(const auto& b:state.bodies)if(!b.events.empty())throw jpc::Error("Saved global playback has local queued events");
            for(unsigned n=0;n<50;++n){const auto& slot=state.playbackPool[n];
                if(slot.owner.empty())continue;const Key key{slot.owner.type,slot.owner.index};const auto found=candidate.find(key);
                if(found==candidate.end())throw jpc::Error("Saved global playback owner is absent");
                const auto& p=found->second.actions.playbacks[n];
                if(slot.active!=p.active||slot.reusable!=p.reusable)throw jpc::Error("Saved global playback owner flags disagree");
            }
            for(const auto& [key,body]:candidate)for(unsigned n=0;n<50;++n){const auto& p=body.actions.playbacks[n];
                if(p.active||!p.reusable){const auto& slot=state.playbackPool[n];
                    if(slot.owner!=edactions::PlaybackOwner{key.type,key.index})throw jpc::Error("Saved global playback ownership overlaps");
                    if(p.active&&p.mode==4&&(p.parent<0||!body.actions.playbacks[size_t(p.parent)].active||body.actions.playbacks[size_t(p.parent)].child!=int(n)))
                        throw jpc::Error("Saved waiting playback parent is invalid");
                    //40582e/40586b leave older incoming links, including foreign slot reuse.
                    // Only waiting mode4 owns the reciprocal link checked above.
                }
            }
        }
        if(globalPlayback)for(const auto& [key,body]:candidate){(void)key;
            for(unsigned n=0;n<50;++n){const auto& p=body.actions.playbacks[n];if(!p.active&&!p.reusable&&p.child>=0)
                throw jpc::Error("Saved reserved completion is not a leaf");
                if(!p.active)continue;std::array<bool,50> seen{};int cursor=int(n);
                while(cursor>=0){if(seen[size_t(cursor)])throw jpc::Error("Saved animation child chain is cyclic");seen[size_t(cursor)]=true;
                    cursor=body.actions.playbacks[size_t(cursor)].child;}
            }
        }
        std::set<std::tuple<int,int,int,int>> interestKeys;
        if(state.boundaryCaptured){size_t resident=0;for(const auto& [key,body]:candidate){(void)key;if(body.allocationOrder<state.levelBoundary)++resident;}
            if(resident!=state.levelBoundary)throw jpc::Error("Saved level allocation boundary has missing resident actors");}
        for(const auto& interest:state.interests) {
            if(interest.indices.empty()||interest.indices.size()>4)throw jpc::Error("Invalid saved actor interest");
            if(!interestKeys.emplace(interest.listenerType,interest.firstType,interest.firstIndex,interest.event).second)
                throw jpc::Error("Duplicate saved actor interest");
            for(int index:interest.indices)if(!candidate.count({interest.listenerType,index}))throw jpc::Error("Saved interest references absent actor");
        }
        std::set<std::tuple<int,int,int,int,int>> messageKeys;
        for(const auto& m:state.messages)if(!messageKeys.emplace(m.first.type,m.first.index,m.second.type,m.second.index,m.event).second)
            throw jpc::Error("Duplicate saved actor message");
        std::set<jpc::Interpreter::ThreadID> callbackIDs;
        for(const auto& callback:state.callbacks) {
            if(!candidate.count(callback.listener))throw jpc::Error("Saved callback references absent actor");
            if(callback.thread) {
                if(!game.vm.threadMatches(callback.thread,1))throw jpc::Error("Saved actor callback thread flags or ownership mismatch");
                if(!callbackIDs.insert(callback.thread).second)throw jpc::Error("Duplicate saved actor callback thread");
            }
        }
        const auto& reservation=state.pickup;
        const auto validTarget=[](const PickupTarget& target){
            if(target.index< -1||target.index>=32)throw jpc::Error("Invalid saved pickup target slot");
            for(float n:target.position)if(!std::isfinite(n))throw jpc::Error("Invalid saved pickup target position");
            if(target.index<0&&(target.setup!=std::array<int,4>{}||target.position!=std::array<float,3>{}))throw jpc::Error("Invalid empty saved pickup target");
        };
        validTarget(reservation.query);validTarget(reservation.target);validTarget(reservation.receipt);
        if(reservation.receiptPhase>2||(reservation.receiptPhase==0)!=(reservation.receipt.index<0))throw jpc::Error("Invalid saved pickup receipt");
        if((reservation.queried?reservation.queryTick!=state.sourceTick:reservation.queryTick!=0||reservation.query.index>=0)||reservation.playback< -1||reservation.playback>=50||
           (reservation.playback<0&&(reservation.state!=-1||reservation.target.index>=0)))throw jpc::Error("Invalid saved pickup reservation");
        if(reservation.queried||reservation.playback>=0||reservation.receiptPhase) {
            const auto ash=candidate.find({0,0});
            if(!game.profile.dreamcast||ash==candidate.end())throw jpc::Error("Saved pickup reservation requires Dreamcast Ash");
            if(reservation.receiptPhase) {
                const auto receiptMessage=[&](Key listener,const Message& message){return listener==Key{0,0}&&message.first==Key{0,0}&&message.second==Key{-1,-1}&&message.event==17&&message.data[0]==reservation.receipt.index;};
                if(!std::any_of(state.messages.begin(),state.messages.end(),[&](const auto& message){return receiptMessage({0,0},message);})&&
                   !std::any_of(state.callbacks.begin(),state.callbacks.end(),[&](const auto& callback){return receiptMessage(callback.listener,callback.message);}))throw jpc::Error("Saved pickup receipt has no message");
            }
            if(reservation.playback>=0) {
                const auto& body=ash->second;const auto& p=body.actions.playbacks[size_t(reservation.playback)];
                if(reservation.state<0||size_t(reservation.state)>=body.actions.states.size()||!p.active||p.state!=reservation.state||
                   !pickupAnimation(body.actions.states[size_t(reservation.state)].animation)||
                   !std::any_of(body.actions.layers.begin(),body.actions.layers.end(),[&](const auto& layer){return layer.state==reservation.state&&layer.playback==reservation.playback;}))
                    throw jpc::Error("Invalid saved pickup reservation playback");
                const auto& clip=body.actions.states[size_t(reservation.state)];
                if(p.frame>=(int(clip.start)+clip.earlyEnd)/2)throw jpc::Error("Saved pickup reservation is past contact");
            }
        }
        auto nextInterests=state.interests;auto nextMessages=state.messages;auto nextCallbacks=state.callbacks;
        actors.swap(candidate);playbackPool=nextPool;animationHeight=state.animationHeight;pickup=state.pickup;for(auto& [key,body]:actors)body.actions.bindPool(poolHooks(key));interests.swap(nextInterests);messages.swap(nextMessages);callbacks.swap(nextCallbacks);
        sourceTick=state.sourceTick;updated=state.updated;levelBoundary=state.levelBoundary;boundaryCaptured=state.boundaryCaptured;sync();
    }
    void sync() {
        for(auto& [key,body]:actors)body.script=script(key);
        auto found=actors.find({0,0});if(found!=actors.end()&&found->second.script) {
            game.ash=found->second.script;game.vm.statics[310]=R(game.ash);game.inventory=game.vm.field(game.ash,99).reference();
        }
    }
    jpc::Ref initializeAsh(int mesh=0x00024000,int skeleton=0x00824000,int texture=0x08024000) {
        if(!actors.empty())throw jpc::Error("Actor registry already initialized");guiPrerequisites();
        game.vm.invoke(game.package.methodIndex("ActorDB","initialize","()V"));
        const auto result=game.vm.invoke(game.package.methodIndex("ActorDB","createAsh","(III)LAsh;"),0,{I(mesh),I(skeleton),I(texture)}).reference();
        if(!result)throw jpc::Error("Original Ash creation failed");sync();return result;
    }
    jpc::Ref createDeadite(int mesh=0x040c6000,int skeleton=0x048c6000,int texture=0x080c6000) {
        const auto result=game.vm.invoke(game.package.methodIndex("ActorDB","createDeadite","(III)LDeadite;"),0,{I(mesh),I(skeleton),I(texture)}).reference();sync();return result;
    }
    jpc::Ref createSkullBat(int mesh=0x04482000,int skeleton=0x04c82000,int texture=0x08482000) {
        const auto result=game.vm.invoke(game.package.methodIndex("ActorDB","createSkullBat","(III)LSkullBat;"),0,{I(mesh),I(skeleton),I(texture)}).reference();sync();return result;
    }
    jpc::Ref createType(int type) {
        const bool variant=levelB&&((type==36&&assets.count(0x06360000))||(type==37&&assets.count(0x06362000)));
        if(type==0||(!loadedTypes.count(type)&&!variant))throw jpc::Error("Unverified ActorDB.createType selection");
        const auto result=game.vm.invoke(game.package.methodIndex("ActorDB","createType","(I)LGenericNpc;"),0,{I(type)}).reference();sync();return result;
    }
    jpc::Ref createSkeleton() {return createType(6);}
    bool consoleAction(Key key,int action) {
        sync();return game.vm.invoke(game.package.methodIndex("ed/base/EDActor","doConsoleAction","(I)Z"),at(key).script,{I(action)}).integer()!=0;
    }
    int playerAction(int logicalAction) {
        sync();const int chosen=game.vm.invoke(game.package.methodIndex("Ash","processPadAction","(I)I"),game.ash,{I(logicalAction)}).integer();
        if(chosen>0)consoleAction({0,0},chosen);return chosen;
    }
    void post(Message message){queue(message);}
    void clearMessages(){
        messages.clear(); // Native40D0F0 clears only the undelivered raw-message queue.
        if(pickup.receiptPhase&&!std::any_of(callbacks.begin(),callbacks.end(),[&](const auto& callback){
            return reservedPickupMessage(callback.listener,callback.message);}))clearPickupReceipt();
    }
    void sampleFloors() {
        // Native402e40/41a3c3: only visible, non-hidden allocation slots query BV;
        // animation freeze40 does not suppress this render-phase cache refresh.
        for(auto* body:registered())if((body->flags&1)&&!(body->flags&8)) {
            body->cachedFloorHeight=actorFloor(body->key,body->pose.x,body->pose.z,body->floorTriangle);
            body->pose.y=body->localY+body->cachedFloorHeight;valid(body->pose);
        }
    }
    void update(uint64_t tick,bool simulate=true,uint32_t threadMask=0) {
        if(!tick||(updated&&(sourceTick==std::numeric_limits<uint64_t>::max()||tick!=sourceTick+1)))throw jpc::Error("Actor source ticks must advance once in order");
        sourceTick=tick;updated=true;sync();
        if(pickup.queried&&pickup.queryTick!=sourceTick){pickup.query={};pickup.queryTick=0;pickup.queried=false;}
        if(const auto ash=actors.find({0,0});ash!=actors.end())refreshPickupTarget(ash->second);
        if(simulate) {
            if(globalPlayback){
                std::map<Key,std::array<edactions::Playback,50>> before;std::set<Key> stepped;
                for(auto* body:ordered())before.emplace(body->key,body->actions.playbacks);
                for(unsigned slot=0;slot<50;++slot){const auto entry=playbackPool.at(slot);if(!entry.active)continue;
                    auto& body=at({entry.owner.type,entry.owner.index});if(!(body.flags&1)||(body.flags&0x40))continue;
                    if(!before.count(body.key))before.emplace(body.key,body.actions.playbacks);
                    if(stepped.insert(body.key).second)body.actions.beginTick();
                    body.actions.tickSlot(slot,[&](unsigned,const auto& playback){animationEvents(body,playback);animationRootHeight(body,playback);});
                    refreshPickupTarget(body);animationContacts(body,before.at(body.key),int(slot));
                }
                for(auto* body:ordered())if((body->flags&1)&&!(body->flags&0x40)){if(body->nativeSlot>=0)actionCallbacks(*body);compose(*body);if(body->nativeSlot>=0&&before.count(body->key))rootMotion(*body,before.at(body->key));}
            }else for(auto* pointer:ordered())if((pointer->flags&1)&&!(pointer->flags&0x40)){
                auto& body=*pointer;const auto before=body.actions.playbacks;
                body.actions.tick([&](unsigned,const edactions::Playback& playback){animationEvents(body,playback);animationRootHeight(body,playback);});
                for(const auto& event:body.actions.events)queue({body.key,{-1,-1},event.event,{}});body.actions.events.clear();
                if(body.nativeSlot>=0)actionCallbacks(body);compose(body);refreshPickupTarget(body);animationContacts(body,before);if(body.nativeSlot>=0)rootMotion(body,before);
            }
            for(auto* body:registered())if(body->key.type==0&&!(body->flags&0x40))
                body->sfxMaterial=required(host.soundMaterials,"soundMaterials")(*body);
            for(auto* pointer:registered())if(pointer->script&&!(pointer->flags&0x40)&&!pointer->think) {auto& body=*pointer;const auto key=body.key;
                body.think=game.vm.enqueue(game.package.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
                game.vm.invoke(game.package.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(body.think))});
            }
            for(auto* pointer:registered())if(!(pointer->flags&0x48)) {auto& body=*pointer;
                required(host.applyMovement,"applyMovement")(body);valid(body.pose);
                if(!host.floorHeightForActor){body.cachedFloorHeight=actorFloor(body.key,body.pose.x,body.pose.z,body.floorTriangle);
                    body.localY=body.pose.y-body.cachedFloorHeight;}
                // The native host preserves localY; subtracting its floor cache loses height precision.
                if(!std::isfinite(body.localY))throw jpc::Error("Non-finite native actor local height");body.pendingMove={};
            }
        }
        if(simulate&&globalPlayback){
            // Native40e3a0 object stage is observationally empty for source
            // A/B JPC: objectvelocity startszero, its writers are not imported;
            // direct objTranslatePosition already changedXYZ synchronously.
            // LIVE406560 ascending sweep AFTER actor/object physics. Command39
            // can allocate/disable a later slot, which this loop must observe.
            for(unsigned slot=0;slot<50;++slot){const auto entry=playbackPool.at(slot);
                if(!entry.active&&!entry.reusable){if(entry.owner.empty())throw jpc::Error("Null source completion owner");
                    at({entry.owner.type,entry.owner.index}).actions.completeSlot(slot);}}
        }
        if(host.soundFrame)host.soundFrame(); //40ce71 also runs while actor simulation is paused.
        auto retry=std::move(callbacks);callbacks.clear();for(const auto& callback:retry) {
            if(callback.thread)callbacks.push_back(callback);else dispatch(callback.listener,callback.message);
        }
        const auto pending=std::move(messages);messages.clear();
        for(const auto& message:pending)for(const auto& interest:interests) {
            if(message.first.type!=-1&&interest.firstType!=-1&&message.first.type!=interest.firstType)continue;
            if(message.first.index!=-1&&interest.firstIndex!=-1&&message.first.index!=interest.firstIndex)continue;
            if(interest.event!=-1&&message.event!=interest.event)continue;
            for(int index:interest.indices)dispatch({interest.listenerType,index},message);
        }
        game.vm.advanceFrame(threadMask);game.vm.runReady(threadMask);collect();
        if(const auto ash=actors.find({0,0});ash!=actors.end())refreshPickupTarget(ash->second);
        for(auto& [key,body]:actors){(void)key;body.vertices=asset(body).mesh.skinPose(body.localPose,body.parts);}
    }
};
} // namespace edactors
