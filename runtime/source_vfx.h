#pragma once
#include "object_mesh.h"
#include "state_bytes.h"
#include "source_actor_math.h"
#include "source_particle_forces.h"
#include <functional>
#include <map>
#include <set>

namespace edvfx {
using Vector=std::array<float,3>;
enum class Blend:uint8_t {Additive,Alpha};
inline constexpr uint32_t CoronaSegments=8,MaxVerticesPerParticle=CoronaSegments*(CoronaSegments+1)*6+12,MaxVertices=1500*MaxVerticesPerParticle;
struct ChildModifier {uint8_t flags=0,kind=0,chance=0;};
struct Definition {
    Blend blend=Blend::Additive;uint32_t nativeRenderer=0;
    uint32_t kind=0,life=0;int32_t cap=-1;
    std::array<uint8_t,4> color{};std::array<int8_t,4> delta{};
    Vector velocity{},acceleration{},positionJitter{};uint32_t jitterMinAge=0;float size=0,sizeMin=0,sizeMax=0,growth=0;
    std::array<float,4> uv{};uint32_t columns=0,rows=0,randomFrame=0;
    Vector dispersion{};float directionScale=0;
    uint32_t countMin=1,countMax=1,frameMode=0,frameDelay=0,collision=0,childDeath=0;
    std::array<uint8_t,3> rotationStep{};Vector bouncePercent{{100,100,100}};uint8_t bounceFlags=0;
    uint32_t frameInitial=std::numeric_limits<uint32_t>::max(),requiredMode=0,ringSegments=0;
    std::array<float,6> ring{};std::array<uint8_t,3> ringColor{};
    uint32_t periodicChild=0,childPeriod=0,childMaxAge=0,childInitial=0;
    uint32_t randomChild=0,randomChildChance=0;std::vector<ChildModifier> children;
};
struct Host {
    std::function<Vector(int)> marker;
    std::function<int(int)> randomBound; // Source418e70, single shared seed step.
    std::function<int(int)> cachedBound; // Source418f20, shared418fc0 table/index.
    std::function<bool(Vector)> visible; // Source category8 general volumes, not camera frustum.
    std::function<int16_t(float,float)> coarseCell;
    std::function<float(float,float)> coarseHeight; // Render426CB0 uses the raw float grid, not the int16 collision gate.
    std::function<double(float,float,float)> floorSample; // Exact41d290 with shared global cache; Y argument ignored by native.
    std::function<Vector(int,int)> actorWorld;
    std::function<edparticle::PhysicalActors()> repellingActors; // Native modifier7 physical13 slots, LOCAL+cached floor.
    std::function<Vector(int,int,unsigned,const std::array<int32_t,3>&)> jointWorld;

};
struct Particle {
    uint32_t kind=0;uint64_t serial=0;Vector position{},velocity{};float size=0;
    int32_t age=0;uint32_t tile=0;std::array<uint8_t,4> color{};
    std::array<uint8_t,3> rotation{};bool stopped=false;
    int32_t renderMode=0;bool deferredDraw=false;
};
struct Batch {
    uint32_t texture=0x0816e044;
    std::array<float,4> tint{};std::vector<edmesh::Vertex32> vertices;
    Blend blend=Blend::Additive; // Legacy state7 default. Native EDVFXD1 carries per-kind original blending.
    uint32_t nativeRenderer=0;
};
class SourceVfx {
public:
    struct Globals {
        std::map<uint32_t,uint32_t> frameTimers,childTimers;
        std::map<uint32_t,int32_t> emitterModes;
        uint32_t suppressedEmissions=0;
        uint64_t nextSerial=1;
    };
private:
    Host host;std::map<uint32_t,Definition> definitions;std::map<uint32_t,std::vector<Particle>> active;
    std::map<uint32_t,uint32_t> frameTimers;
    std::map<uint32_t,uint32_t> childTimers;
    uint32_t texture=0,pool=0,assetHash=2166136261u,suppressedEmissions=0;uint64_t tick=0,nextSerial=1;bool updated=false;
    struct RingMode {std::array<float,6> curve{};std::array<uint8_t,3> color{};uint8_t segments=0;};
    std::map<uint32_t,RingMode> ringModes;
    std::map<uint32_t,int32_t> emitterModes;
    bool treeEffects=false,variantEffects=false,renderSampled=false;uint64_t sampledTick=0;
    struct CoronaMode {std::array<float,2> size{};std::array<uint8_t,3> color{};uint8_t segments=8,hemisphere=0;};
    std::map<int32_t,CoronaMode> coronaModes;
    struct RenderMutation {Vector position{};int32_t age=0;};
    std::map<uint64_t,RenderMutation> renderPending;
    std::array<float,2> coronaSize{};std::array<uint8_t,3> coronaColor{};
    static void finite(Vector v){for(float x:v)if(!std::isfinite(x))throw std::runtime_error("Non-finite source VFX position");}
    static void finite(float x){if(!std::isfinite(x))throw std::runtime_error("Non-finite source VFX value");}
    int random(bool cached,int bound){auto& fn=cached?host.cachedBound:host.randomBound;
        if(!fn)throw std::runtime_error("Source VFX RNG capability is missing");const int value=fn(bound);
        if(value<0||(bound?int64_t(value)>=std::abs(int64_t(bound)):value!=0))throw std::runtime_error("Source VFX RNG outside native bound");return value;}
    size_t count()const{size_t result=0;for(const auto& [kind,particles]:active){(void)kind;result+=particles.size();}return result;}
    static Definition readDefinition(edobjects::detail::Reader& input) {
        Definition d;d.kind=input.get<uint32_t>();d.cap=input.get<int32_t>();d.life=input.get<uint32_t>();
        for(auto& v:d.color)v=input.get<uint8_t>();for(auto& v:d.delta)v=input.get<int8_t>();
        for(auto& v:d.velocity)v=input.get<float>();for(auto& v:d.acceleration)v=input.get<float>();
        d.size=input.get<float>();d.sizeMin=input.get<float>();d.sizeMax=input.get<float>();d.growth=input.get<float>();
        for(auto& v:d.uv)v=input.get<float>();d.columns=input.get<uint32_t>();d.rows=input.get<uint32_t>();d.randomFrame=input.get<uint32_t>();return d;
    }
    static Definition readExtended(edobjects::detail::Reader& input) {
        auto d=readDefinition(input);for(auto& v:d.dispersion)v=input.get<float>();d.directionScale=input.get<float>();
        d.countMin=input.get<uint32_t>();d.countMax=input.get<uint32_t>();d.frameMode=input.get<uint32_t>();d.frameDelay=input.get<uint32_t>();
        d.collision=input.get<uint32_t>();d.childDeath=input.get<uint32_t>();for(auto& v:d.rotationStep)v=input.get<uint8_t>();
        d.bounceFlags=input.get<uint8_t>();if(d.bounceFlags>1)throw std::runtime_error("Unrecovered source bounce flags");for(auto& v:d.bouncePercent)v=input.get<float>();return d;
    }
    void addDefinition(const Definition& d,int scope) {
        const bool kindAllowed=scope==0?(d.kind==9||d.kind==23):scope==1?(d.kind==2||d.kind==18||d.kind==19):
            scope==2?(d.kind==0||d.kind==25):scope==3?(d.kind==7||d.kind==22):scope==4?(d.kind==15||d.kind==24):scope==5?(d.kind==6||d.kind==8||d.kind==20):scope==6?(d.kind==3||d.kind==32):scope==7&&(d.kind==16||d.kind==17||d.kind==21);
        if(!kindAllowed||d.cap< -1||d.cap>int(pool)||
           (!d.life&&d.kind!=2&&d.kind!=8&&d.kind!=20&&d.kind!=21&&d.kind!=32)||d.life>32766||!d.columns||!d.rows||d.columns>256||d.rows>256||uint64_t(d.columns)*d.rows>256||d.randomFrame>1||
           !d.countMin||d.countMax<d.countMin||d.countMax>1500||(d.frameMode!=0&&d.frameMode!=2)||d.frameDelay>32767||
           (d.frameInitial!=std::numeric_limits<uint32_t>::max()&&d.frameInitial>d.frameDelay)||d.collision>2||(d.bounceFlags&&(d.kind!=8||d.collision!=2))||d.childDeath>34||
           d.requiredMode!=(d.kind==25?3u:0u)||d.ringSegments!=(d.kind==25?16u:0u))
            throw std::runtime_error("Unsupported source VFX definition");
        finite(d.velocity);finite(d.acceleration);finite(d.dispersion);finite(d.bouncePercent);finite(d.positionJitter);
        if(d.kind==16?(d.jitterMinAge!=16||d.positionJitter!=Vector{{.05f,.05f,.05f}}):(d.jitterMinAge||d.positionJitter!=Vector{}))
            throw std::runtime_error("Unrecovered source positional jitter");
        for(float value:d.ring)finite(value);
        for(float x:{d.size,d.sizeMin,d.sizeMax,d.growth,d.directionScale,d.uv[0],d.uv[1],d.uv[2],d.uv[3]})finite(x);
        if(d.sizeMin<0||d.sizeMax<d.sizeMin||d.size<0||d.uv[0]<0||d.uv[1]<0||d.uv[2]>1||d.uv[3]>1||d.uv[0]>=d.uv[2]||d.uv[1]>=d.uv[3]||
           !definitions.emplace(d.kind,d).second)throw std::runtime_error("Invalid source VFX timeline/atlas");
        if(d.frameMode)frameTimers.emplace(d.kind,d.frameInitial==std::numeric_limits<uint32_t>::max()?d.frameDelay:d.frameInitial);
        if(d.periodicChild){if(d.kind!=7||d.periodicChild!=22||d.childPeriod!=2||d.childMaxAge!=5||d.childInitial>d.childPeriod)
                throw std::runtime_error("Unrecovered source periodic VFX child");childTimers.emplace(d.kind,d.childInitial);}
        else if(d.childPeriod||d.childMaxAge||d.childInitial)throw std::runtime_error("Invalid source VFX child timer");
        if(d.randomChildChance&&!((d.kind==15&&d.randomChild==0&&d.randomChildChance==25)||(d.kind==20&&d.randomChild==18&&d.childDeath==18&&d.randomChildChance==5)||(d.kind==17&&d.randomChild==32&&d.randomChildChance==75)))throw std::runtime_error("Unrecovered source random VFX child");
        if(!d.children.empty()) {
            const std::vector<ChildModifier> original=d.kind==32?std::vector<ChildModifier>{{4,0,25},{4,22,15},{1,0,0}}:
                // DC8c092120 has one modifier31 (child9/chance5); the PC constructor has two.
                d.kind==16?(d.children.size()==1?std::vector<ChildModifier>{{4,9,5}}:std::vector<ChildModifier>{{4,9,15},{4,22,2}}):std::vector<ChildModifier>{};
            if(original.empty()||d.children.size()!=original.size())throw std::runtime_error("Unrecovered source child modifiers");
            for(unsigned i=0;i<original.size();++i)if(d.children[i].flags!=original[i].flags||d.children[i].kind!=original[i].kind||d.children[i].chance!=original[i].chance)
                throw std::runtime_error("Unrecovered source child modifier parameters");
        } else if(d.kind==32||d.kind==16)throw std::runtime_error("Missing source child modifiers");
    }
    void hash(const std::vector<uint8_t>& bytes){for(auto byte:bytes)assetHash=(assetHash^byte)*16777619u;}
    Batch makeBatch(uint32_t kind)const {const auto& definition=definitions.at(kind);Batch batch;batch.texture=texture;batch.blend=definition.blend;batch.nativeRenderer=definition.nativeRenderer;return batch;}
    static int16_t floorWord(double value) {
        if(!std::isfinite(value)||value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Source VFX floor exceeds native integer range");
        const auto low=uint32_t(int32_t(value))&65535u;return int16_t(low<32768?int32_t(low):int32_t(low)-65536);
    }
    void collide(Particle& p,const Definition& d) {
        if(!host.coarseCell||!host.floorSample)throw std::runtime_error("Source VFX floor capability is missing");
        const auto coarse=host.coarseCell(p.position[0],p.position[2]);
        if(d.collision==1) { // Native32: the coarse gate itself kills; actual floor is used only for final position.
            if(p.position[1]<coarse||(coarse==32767&&p.position[1]<-100)) {
                p.position[1]=float(host.floorSample(p.position[0],float(coarse),p.position[2]));finite(p.position);p.age=-1;
            }
        } else if(coarse!=32767&&p.position[1]<coarse) {
            // Native1 truncates41d290's result to signed LOW16 before bounce.
            const auto floor=floorWord(host.floorSample(p.position[0],32767,p.position[2]));
            if(floor==32767)p.age=-1;
            else if(floor>p.position[1]) {
                p.position[1]=float(floor);p.velocity[1]=-p.velocity[1];constexpr float epsilon=.009999999776482582f;
                // Native4100BF: flag1 suppresses the stopped bit; shell8 keeps moving after tiny bounces.
                if(p.velocity[1]>-epsilon&&p.velocity[1]<epsilon){p.velocity[1]=0;if(!d.bounceFlags)p.stopped=true;}
                for(unsigned axis=0;axis<3;++axis)if(d.bouncePercent[axis]!=100) {
                    const double value=double(p.velocity[axis])*d.bouncePercent[axis]*epsilon;
                    p.velocity[axis]=axis!=1&&value>-epsilon&&value<epsilon?0:float(value);
                }
            }
        }
    }
    Batch ring(const Particle& p,const Definition& source)const {
        auto d=source;if(treeEffects){const auto& mode=ringModes.at(uint32_t(p.renderMode));d.ring=mode.curve;d.ringColor=mode.color;d.ringSegments=mode.segments;}
        auto batch=makeBatch(p.kind);
        const float radius=float(double(p.age)*d.ring[1]*.0625+d.ring[0]);
        const float amplitude=float(double(p.age)*d.ring[5]*.0625+d.ring[4]);
        const double angle=double(p.age)*d.ring[3]*.0625+d.ring[2];
        const float outer=float(std::cos(angle)*amplitude+radius),height=float(std::sin(angle)*amplitude);
        for(unsigned c=0;c<3;++c)batch.tint[c]=float(std::max(0,int(d.ringColor[c])-((int(d.ringColor[c])*p.age)>>4)))/255;
        batch.tint[3]=float((0xff000000u-(uint32_t(p.age)<<28))>>24)/255;
        Vector innerPrevious{{p.position[0],p.position[1],p.position[2]+radius}},
               outerPrevious{{p.position[0],p.position[1]+height,p.position[2]+outer}};
        for(unsigned segment=1;segment<=d.ringSegments;++segment) {
            const double theta=segment==d.ringSegments?0:double(segment)*edactormath::Tau/d.ringSegments;
            const float sine=float(std::sin(theta));const double cosine=std::cos(theta);
            const Vector inner{{float(double(sine)*radius+p.position[0]),p.position[1],float(cosine*radius+p.position[2])}},
                         edge{{float(double(sine)*outer+p.position[0]),p.position[1]+height,float(cosine*outer+p.position[2])}};
            const std::array<Vector,4> points{{innerPrevious,inner,edge,outerPrevious}};
            std::array<edmesh::Vertex32,4> quad{};
            for(unsigned i=0;i<4;++i) {auto& vertex=quad[i];vertex.x=points[i][0];vertex.y=points[i][1];vertex.z=points[i][2];
                vertex.u=(i==1||i==2)?d.uv[2]:d.uv[0];vertex.v=i<2?d.uv[3]:d.uv[1];}
            for(unsigned index:{0u,1u,2u,0u,2u,3u})batch.vertices.push_back(quad[index]);innerPrevious=inner;outerPrevious=edge;
        }
        return batch;
    }
    static int sar4(int value){return value>=0?value/16:-((-value+15)/16);}
    void applyRenderPending() {
        if(renderPending.empty())return;
        if(const auto group=active.find(24);group!=active.end())for(auto& p:group->second)if(const auto at=renderPending.find(p.serial);at!=renderPending.end()) {
            p.position=at->second.position;p.age=at->second.age;
        }
        renderPending.clear();
    }
    Batch coronaSecondary(const Particle& p,int32_t uvAge)const {
        const auto& mode=coronaModes.at(p.renderMode);auto batch=makeBatch(p.kind);batch.vertices.reserve(MaxVerticesPerParticle-12);
        // compatibility repair:42795E/966/975 uses these computed RGB values
        // as offsets into neighboring native memory. Use the clamped values as
        // colors; retain original geometry, UVs, alpha wrapping and timing.
        for(unsigned c=0;c<3;++c)batch.tint[c]=float(std::clamp(int(mode.color[c])-sar4(int(mode.color[c])*p.age),0,255))/255;
        batch.tint[3]=float((0xff000000u-(uint32_t(p.age)<<28))>>24)/255;
        const double angle=double(uvAge)*.01745329238474369f,sineUV=std::sin(angle),cosineUV=std::cos(double(float(angle)));
        const float left=float((112-16*sineUV)/256),right=float((112+16*sineUV)/256),
                    top=float((208+16*cosineUV)/256),bottom=float((208+16*sineUV)/256);
        const float radius=float(double(mode.size[0])+double(p.age)*mode.size[1]*.0625);
        const unsigned count=mode.segments,latitudes=mode.hemisphere?(count-1)/2+1:count;const double step=double(edactormath::Pi)/count;
        for(unsigned i=0;i<latitudes;++i){const float a=float(double(edactormath::HalfPi)-i*step);
            const double b=double(edactormath::HalfPi)-(i+1)*step;
            const float y0=float(std::sin(double(a))*radius),r0=float(std::cos(double(a))*radius),
                        y1=float(std::sin(b)*radius),r1=float(std::cos(b)*radius);
            Vector previous0{{p.position[0],p.position[1]+y0,p.position[2]+r0}},
                   previous1{{p.position[0],p.position[1]+y1,p.position[2]+r1}};
            // j0 intentionally emits the original degenerate first quad.
            for(unsigned j=0;j<=count;++j){const double theta=j==count?0:double(edactormath::Tau)/count*j;
                const float sine=float(std::sin(theta));const double cosine=std::cos(theta);
                const Vector current0{{float(double(p.position[0])+double(sine)*r0),p.position[1]+y0,float(double(p.position[2])+cosine*r0)}},
                             current1{{float(double(p.position[0])+double(sine)*r1),p.position[1]+y1,float(double(p.position[2])+cosine*r1)}};
                const std::array<Vector,4> points{{previous0,current0,current1,previous1}};std::array<edmesh::Vertex32,4> quad{};
                for(unsigned n=0;n<4;++n){quad[n].x=points[n][0];quad[n].y=points[n][1];quad[n].z=points[n][2];
                    quad[n].u=n==0||n==3?left:right;quad[n].v=n<2?top:bottom;}
                for(unsigned index:{0u,1u,2u,0u,2u,3u})batch.vertices.push_back(quad[index]);previous0=current0;previous1=current1;
            }
        }
        return batch;
    }
    bool emit(uint32_t kind,Vector position,Vector endpoint,int mode) {
        const bool temporaryDebrisOverride=kind==15&&mode==1;
        // Native411CA0 writes explicit wrapper modes before411D9C suppresses
        // allocation/type dispatch; raw mode-1 inherits only the stored mode.
        if(suppressedEmissions){
            if(treeEffects&&emitterModes.count(kind)&&mode>=0){
                if(kind==24)mode=std::clamp(mode,0,6);if(kind==25)mode=std::clamp(mode,0,13);
                if(kind==15&&mode>2)throw std::runtime_error("Unrecovered suppressed source VFX variant");
                emitterModes[kind]=mode;
            }
            --suppressedEmissions;return false;
        }
        finite(position);finite(endpoint);const auto at=definitions.find(kind);
        if(at==definitions.end())throw std::runtime_error("Unrecovered source VFX kind/render mode");
        if(treeEffects&&emitterModes.count(kind)) {
            if(mode< -1)throw std::runtime_error("Invalid source VFX render mode");
            const bool explicitMode=mode>=0;
            if(explicitMode){if(kind==24)mode=std::clamp(mode,0,6);if(kind==25)mode=std::clamp(mode,0,13);}
            else mode=emitterModes.at(kind);
            if((kind==15&&mode>2)||(kind==24&&!coronaModes.count(mode))||(kind==25&&!ringModes.count(uint32_t(mode))))
                throw std::runtime_error("Unrecovered source VFX render variant");
            if(explicitMode)emitterModes[kind]=mode;
        } else {
            if(mode==-1&&(kind==0||kind==6||kind==7||kind==8||kind==9||kind==16||kind==17||kind==20||kind==21))mode=0;
            if(mode<0||uint32_t(mode)!=at->second.requiredMode)throw std::runtime_error("Unrecovered source VFX kind/render mode");
        }
        const auto& d=at->second;auto& particles=active[kind];
        if(d.cap>=0&&particles.size()>=unsigned(d.cap))return false;
        const unsigned amount=d.countMin+(d.countMax!=d.countMin?unsigned(random(false,int(d.countMax-d.countMin))):0);
        Vector velocity=d.velocity;for(unsigned axis=0;axis<3;++axis)velocity[axis]+=float((double(endpoint[axis])-position[axis])*d.directionScale);
        bool emitted=false;
        for(unsigned n=0;n<amount;++n) {
            if(count()>=pool)continue; // Native emitter skips refused slots without per-particle RNG.
            if(nextSerial==std::numeric_limits<uint64_t>::max())throw std::runtime_error("Source VFX serial overflow");
            Particle p;p.kind=kind;p.serial=nextSerial;p.position=position;p.velocity=velocity;p.color=d.color;p.renderMode=mode;
            auto dispersion=d.dispersion;if(temporaryDebrisOverride){dispersion={{.09999999403953552f,1.0499999523162842f,.09999999403953552f}};p.color={{255,255,255,255}};}
            constexpr float inverse32767=3.0518509447574615e-05f;
            for(unsigned axis=0;axis<3;++axis)if(dispersion[axis])
                p.velocity[axis]=float((double(random(false,32767))*inverse32767-.5)*dispersion[axis]+velocity[axis]);
            p.size=d.size?d.size:float(double(random(false,32767))*inverse32767*(double(d.sizeMax)-d.sizeMin)+d.sizeMin);
            p.tile=d.randomFrame?uint32_t(random(true,int(d.columns*d.rows))):0;
            ++nextSerial;particles.insert(particles.begin(),p);emitted=true; // Native40fbc1 inserts at group head.
        }
        return emitted;
    }
public:
    SourceVfx(const std::filesystem::path& root,Host callbacks):host(std::move(callbacks)){
        const auto bytes=edobjects::detail::read(root/"vfx/effects.bin",1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFX1\0\0");
        texture=input.get<uint32_t>();pool=input.get<uint32_t>();const auto count=input.get<uint32_t>();
        if(texture!=0x0816e044||pool!=1500||!count||count>35)throw std::runtime_error("Invalid source VFX header");
        for(unsigned i=0;i<count;++i)addDefinition(readDefinition(input),false);
        if(input.cursor!=bytes.size())throw std::runtime_error("Trailing source VFX definitions");hash(bytes);
        if(std::filesystem::exists(root/"vfx/hit-effects.bin")) {
            const auto hits=edobjects::detail::read(root/"vfx/hit-effects.bin",1024*1024);edobjects::detail::Reader hit{hits};hit.magic("EDVFXH1\0");
            if(hit.get<uint32_t>()!=3)throw std::runtime_error("Invalid source hit VFX count");
            for(unsigned n=0;n<3;++n)addDefinition(readExtended(hit),1);
            if(hit.cursor!=hits.size())throw std::runtime_error("Trailing source hit VFX definitions");hash(hits);
            for(const auto& [kind,d]:definitions)if(d.childDeath&&!supports(d.childDeath))throw std::runtime_error("Missing source VFX recursive child");
        }
        if(std::filesystem::exists(root/"vfx/emitter-effects.bin")) {
            const auto emitterBytes=edobjects::detail::read(root/"vfx/emitter-effects.bin",1024*1024);edobjects::detail::Reader emitInput{emitterBytes};emitInput.magic("EDVFXE1\0");
            if(emitInput.get<uint32_t>()!=2)throw std::runtime_error("Invalid source actor-emitter VFX count");
            for(unsigned n=0;n<2;++n){auto d=readExtended(emitInput);d.frameInitial=emitInput.get<uint32_t>();d.requiredMode=emitInput.get<uint32_t>();
                for(auto& value:d.ring)value=emitInput.get<float>();for(auto& color:d.ringColor)color=emitInput.get<uint8_t>();d.ringSegments=emitInput.get<uint8_t>();addDefinition(d,2);}
            if(emitInput.cursor!=emitterBytes.size())throw std::runtime_error("Trailing source actor-emitter VFX definitions");hash(emitterBytes);
        }
        if(std::filesystem::exists(root/"vfx/ranged-effects.bin")) {
            const auto ranged=edobjects::detail::read(root/"vfx/ranged-effects.bin",1024*1024);edobjects::detail::Reader inputRanged{ranged};inputRanged.magic("EDVFXR1\0");
            if(inputRanged.get<uint32_t>()!=2)throw std::runtime_error("Invalid source ranged VFX count");
            for(unsigned n=0;n<2;++n){auto d=readExtended(inputRanged);d.periodicChild=inputRanged.get<uint32_t>();d.childPeriod=inputRanged.get<uint32_t>();
                d.childMaxAge=inputRanged.get<uint32_t>();d.childInitial=inputRanged.get<uint32_t>();addDefinition(d,3);}
            if(inputRanged.cursor!=ranged.size()||!supports(7)||!supports(22))throw std::runtime_error("Invalid source ranged VFX definitions");hash(ranged);
        }
        if(std::filesystem::exists(root/"vfx/tree-effects.bin")) {
            if(!supports(0)||!supports(9)||!supports(22)||!supports(25))throw std::runtime_error("Missing source Tree VFX dependency");
            const auto data=edobjects::detail::read(root/"vfx/tree-effects.bin",1024*1024);edobjects::detail::Reader source{data};source.magic("EDVFXT1\0");
            if(source.get<uint32_t>()!=2)throw std::runtime_error("Invalid source Tree VFX count");
            for(unsigned n=0;n<2;++n){auto d=readExtended(source);d.randomChild=source.get<uint32_t>();d.randomChildChance=source.get<uint32_t>();addDefinition(d,4);}
            const auto& base=definitions.at(25);ringModes.emplace(3,RingMode{base.ring,base.ringColor,uint8_t(base.ringSegments)});
            if(source.get<uint32_t>()!=2)throw std::runtime_error("Invalid source Tree ring variants");
            for(unsigned n=0;n<2;++n){const auto index=source.get<uint32_t>();RingMode mode;for(auto& value:mode.curve){value=source.get<float>();finite(value);}
                for(auto& c:mode.color)c=source.get<uint8_t>();mode.segments=source.get<uint8_t>();
                if((index!=0&&index!=2)||mode.segments!=16||!ringModes.emplace(index,mode).second)throw std::runtime_error("Invalid source Tree ring variant");}
            if(source.get<uint32_t>()!=4)throw std::runtime_error("Unrecovered source corona variant");
            for(auto& value:coronaSize){value=source.get<float>();finite(value);}for(auto& c:coronaColor)c=source.get<uint8_t>();
            if(source.get<uint8_t>())throw std::runtime_error("Invalid source corona color padding");
            if(source.get<uint32_t>()!=1||source.get<uint32_t>()!=0x01000801||source.cursor!=data.size())throw std::runtime_error("Invalid source Tree corona definition");
            coronaModes.emplace(4,CoronaMode{coronaSize,coronaColor,8,1});
            emitterModes={{15,0},{24,0},{25,0}};treeEffects=true;hash(data);
        }
        if(std::filesystem::exists(root/"vfx/ash-effects.bin")) {
            if(!supports(18))throw std::runtime_error("Missing source Ash VFX child18");
            const auto data=edobjects::detail::read(root/"vfx/ash-effects.bin",1024*1024);edobjects::detail::Reader source{data};source.magic("EDVFXA1\0");
            const auto ashCount=source.get<uint32_t>();if(ashCount<1||ashCount>3)throw std::runtime_error("Invalid source Ash VFX count");
            for(unsigned n=0;n<ashCount;++n){auto d=readExtended(source);d.randomChild=source.get<uint32_t>();d.randomChildChance=source.get<uint32_t>();addDefinition(d,5);}
            if(!supports(20)||(ashCount>=2&&!supports(8))||(ashCount==3&&!supports(6))||source.cursor!=data.size())throw std::runtime_error("Invalid source Ash VFX definitions");hash(data);
        }
        if(std::filesystem::exists(root/"vfx/variant-effects.bin")) {
            if(!treeEffects||!supports(0)||!supports(22)||!supports(24)||!supports(25))throw std::runtime_error("Missing source variant VFX dependencies");
            const auto data=edobjects::detail::read(root/"vfx/variant-effects.bin",1024*1024);edobjects::detail::Reader source{data};source.magic("EDVFXV1\0");
            if(source.get<uint32_t>()!=2)throw std::runtime_error("Invalid source variant definition count");
            for(unsigned n=0;n<2;++n){auto d=readExtended(source);const auto children=source.get<uint32_t>();if(children>3)throw std::runtime_error("Excessive source child modifiers");
                for(unsigned i=0;i<children;++i){ChildModifier child;child.flags=source.get<uint8_t>();child.kind=source.get<uint8_t>();child.chance=source.get<uint8_t>();
                    if(source.get<uint8_t>())throw std::runtime_error("Invalid source child padding");d.children.push_back(child);}addDefinition(d,6);}
            if(!supports(3)||!supports(32))throw std::runtime_error("Missing source variant child definition");
            const auto rings=source.get<uint32_t>();if(rings!=1&&rings!=3)throw std::runtime_error("Invalid source ring variant count");
            for(unsigned i=0;i<rings;++i){const auto index=source.get<uint32_t>();RingMode ring;
                for(auto& x:ring.curve){x=source.get<float>();finite(x);}for(auto& c:ring.color)c=source.get<uint8_t>();ring.segments=source.get<uint8_t>();
                if((index!=1&&!(rings==3&&(index==11||index==12)))||ring.segments!=(index==12?8:16)||!ringModes.emplace(index,ring).second)
                    throw std::runtime_error("Invalid source ring geometry");}
            const auto coronas=source.get<uint32_t>();if(coronas<1||coronas>2)throw std::runtime_error("Invalid source corona count");
            for(unsigned i=0;i<coronas;++i){const auto index=source.get<uint32_t>();if(index!=1&&!(coronas==2&&index==6))throw std::runtime_error("Invalid source corona variant");
                CoronaMode corona;for(auto& x:corona.size){x=source.get<float>();finite(x);}for(auto& c:corona.color)c=source.get<uint8_t>();
                if(source.get<uint8_t>()||source.get<uint32_t>()!=1||source.get<uint8_t>()!=(index==6?1:0))throw std::runtime_error("Unrecovered source corona row");
                corona.segments=source.get<uint8_t>();if(source.get<uint8_t>())throw std::runtime_error("Invalid source corona row padding");corona.hemisphere=source.get<uint8_t>();
                if(corona.segments!=CoronaSegments||corona.hemisphere!=(index==6?1:0)||!coronaModes.emplace(int32_t(index),corona).second)
                    throw std::runtime_error("Invalid source corona geometry");}
            if(source.cursor!=data.size())throw std::runtime_error("Trailing source variant definitions");
            variantEffects=true;hash(data);
        }
        if(std::filesystem::exists(root/"vfx/npc-effects.bin")) {
            if(!supports(32))throw std::runtime_error("Missing source NPC VFX child32");
            const auto data=edobjects::detail::read(root/"vfx/npc-effects.bin",1024*1024);edobjects::detail::Reader source{data};source.magic("EDVFXN1\0");
            const auto npcs=source.get<uint32_t>();if(npcs<1||npcs>3)throw std::runtime_error("Invalid source NPC VFX count");
            for(unsigned n=0;n<npcs;++n){auto d=readExtended(source);d.randomChild=source.get<uint32_t>();d.randomChildChance=source.get<uint32_t>();
                if(d.kind==21&&!supports(18))throw std::runtime_error("Missing source21 child18");
                if(d.kind==16){if(!supports(9)||!supports(22))throw std::runtime_error("Missing source16 children");d.jitterMinAge=source.get<uint32_t>();for(auto& v:d.positionJitter)v=source.get<float>();
                    const auto children=source.get<uint32_t>();if(children!=1&&children!=2)throw std::runtime_error("Invalid source16 child count");
                    for(unsigned i=0;i<children;++i){ChildModifier child;child.flags=source.get<uint8_t>();child.kind=source.get<uint8_t>();child.chance=source.get<uint8_t>();
                        if(source.get<uint8_t>())throw std::runtime_error("Invalid source16 child padding");d.children.push_back(child);}}
                addDefinition(d,7);}
            if(!supports(17)||source.cursor!=data.size())throw std::runtime_error("Invalid source NPC VFX definitions");hash(data);
        }
        if(std::filesystem::exists(root/"vfx/render-states.bin")) {
            // shortcut: native parameters/blending/primitive choice are restored; update/RNG/projection remain the host bridge until original PVR timeline/raster captures verify parity.
            const auto data=edobjects::detail::read(root/"vfx/render-states.bin",4096);edobjects::detail::Reader source{data};source.magic("EDVFXD1\0");
            constexpr uint8_t originalSha[]={0xde,0x26,0xcb,0xa0,0xd4,0xe6,0x03,0x5d,0x11,0x28,0xda,0xf5,0x04,0xa2,0x25,0x7a,0x69,0x50,0x50,0xba,0xc1,0x1e,0xcd,0xf6,0xe9,0xe2,0xe0,0xba,0xf0,0xf2,0x68,0x41};
            for(auto byte:originalSha)if(source.get<uint8_t>()!=byte)throw std::runtime_error("Unknown native VFX executable identity");
            if(source.get<uint32_t>()!=definitions.size())throw std::runtime_error("Incomplete native VFX draw table");std::set<uint32_t> seen;
            for(unsigned i=0;i<definitions.size();++i){const auto kind=source.get<uint32_t>(),src=source.get<uint32_t>(),dst=source.get<uint32_t>(),renderer=source.get<uint32_t>();
                const bool knownRenderer=renderer==0x8c0904c0||renderer==0x8c08caa0||renderer==0x8c08dda0||renderer==0x8c08c300||renderer==0x8c08fca0||renderer==0x8c090920||renderer==0x8c08f000||renderer==0x8c08e520;
                if(!supports(kind)||!seen.insert(kind).second||src!=8||(dst!=6&&dst!=10)||!knownRenderer)throw std::runtime_error("Unrecovered native VFX draw context");
                auto& d=definitions.at(kind);d.blend=dst==6?Blend::Alpha:Blend::Additive;d.nativeRenderer=renderer;
            }
            if(source.cursor!=data.size())throw std::runtime_error("Trailing native VFX draw context");hash(data);
        }
    }
    Globals globals()const{return {frameTimers,childTimers,emitterModes,suppressedEmissions,nextSerial};}
    void restoreGlobals(const Globals& state){
        // Process-global411430 state survives the fresh script/actor context.
        // Import before destination script activation, after scene particles clear.
        if(count()||!renderPending.empty()||!state.nextSerial||state.suppressedEmissions>60||
           state.frameTimers.size()!=frameTimers.size()||state.childTimers.size()!=childTimers.size()||
           state.emitterModes.size()!=emitterModes.size())throw std::runtime_error("Invalid source VFX global transfer");
        for(const auto& [kind,value]:state.frameTimers)
            if(!frameTimers.count(kind)||value>definitions.at(kind).frameDelay)throw std::runtime_error("Invalid transferred VFX frame timer");
        for(const auto& [kind,value]:state.childTimers)
            if(!childTimers.count(kind)||value>definitions.at(kind).childPeriod)throw std::runtime_error("Invalid transferred VFX child timer");
        for(const auto& [kind,mode]:state.emitterModes)
            if(!emitterModes.count(kind)||mode<0||(kind==15&&mode>2)||(kind==24&&mode>6)||
               (kind==25&&mode>13))throw std::runtime_error("Invalid transferred VFX emitter mode");
        auto frames=state.frameTimers,children=state.childTimers;auto modes=state.emitterModes;
        frameTimers.swap(frames);childTimers.swap(children);emitterModes.swap(modes);
        suppressedEmissions=state.suppressedEmissions;nextSerial=state.nextSerial;
    }
    bool supports(uint32_t kind)const{return definitions.count(kind)!=0;}
    const std::map<uint32_t,std::vector<Particle>>& particles()const{return active;}
    void clear(){active.clear();renderPending.clear();renderSampled=false;} // Source411430 retains global emitter modes/timers.
    void clearAndSuppress(uint32_t attempts=60){
        if(attempts>60)throw std::runtime_error("Unverified source portal suppression count");
        clear();suppressedEmissions=attempts; // Native411BB0 then411BA0(60); not a tick timer.
    }
    bool spawn(uint32_t kind,Vector position,int mode=0){
        return emit(kind,position,position,mode);
    }
    bool line(uint32_t kind,Vector first,Vector second,int mode=0){return emit(kind,first,second,mode);}
    uint32_t childPhase(uint32_t kind)const{return childTimers.at(kind);}
    bool request(const std::string& name,const std::vector<int32_t>& args){
        if(name=="vfxSpawnActorJointRnd"&&args.size()==7) {
            if(!host.jointWorld)throw std::runtime_error("Source VFX joint capability is missing");
            if(args[3]<0||args[4]>4096)throw std::runtime_error("Source VFX joint/repetition outside native-work budget");
            finite(host.jointWorld(args[1],args[2],unsigned(args[3]),{})); // Native resolves actor/bone even when repeat<=0.
            bool emitted=false;const int64_t half=int64_t(args[5])/2-((args[5]<0&&(args[5]&1))?1:0);
            for(int32_t n=0;n<args[4];++n) {
                std::array<int32_t,3> cm{};for(auto& offset:cm){const int64_t value=int64_t(random(false,args[5]))-half;
                    if(value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())throw std::runtime_error("Source joint offset overflow");offset=int32_t(value);}
                const auto point=host.jointWorld(args[1],args[2],unsigned(args[3]),cm);
                emitted=emit(uint32_t(args[0]),point,point,args[6])||emitted;
            }
            return emitted;
        }
        if(name=="vfxSpawnActorOffset"&&args.size()==7) {
            //411907: AC05 kind15/mode1 points downward from the requested
            // WORLD offset. Kind25 does not use the native aux+D8 endpoint.
            if((args[0]!=25&&!(args[0]==15&&args[6]==1))||!host.actorWorld)throw std::runtime_error("Unrecovered source actor-offset VFX capability");
            auto point=host.actorWorld(args[1],args[2]);for(unsigned axis=0;axis<3;++axis)
                point[axis]=float(double(point[axis])+double(args[axis+3])*.009999999776482582f);
            auto endpoint=point;if(args[0]==15)endpoint[1]-=1.f;
            return emit(uint32_t(args[0]),point,endpoint,args[6]);
        }
        if(name=="vfxSpawnActorJoint"&&args.size()==7) {
            if(!host.jointWorld)throw std::runtime_error("Source VFX joint capability is missing");
            if(args[3]<0)throw std::runtime_error("Negative source VFX joint index");
            const auto point=host.jointWorld(args[1],args[2],unsigned(args[3]),{{args[4],args[5],args[6]}});
            return emit(uint32_t(args[0]),point,point,0); // Native411b5a copies the transformed joint into BOTH endpoints.
        }
        if(!host.marker)throw std::runtime_error("Source VFX marker capability is missing");
        if(name=="vfxSpawnMarker"&&args.size()==3)return emit(uint32_t(args[0]),host.marker(args[1]),host.marker(args[2]),0);
        if(name!="vfxSpawnMarkerOffset"||args.size()!=7)throw std::runtime_error("Unrecovered source VFX emitter ABI");
        auto point=host.marker(args[1]),endpoint=host.marker(args[2]);
        constexpr float centimeter=.009999999776482582f;
        for(unsigned axis=0;axis<3;++axis) {point[axis]=float(double(point[axis])+double(args[axis+3])*centimeter);
            endpoint[axis]=float(double(endpoint[axis])+double(args[axis+3])*centimeter);}
        return emit(uint32_t(args[0]),point,endpoint,args[6]);
    }
    void update(uint64_t sourceTick){
        if(!sourceTick||(updated&&sourceTick<=tick))throw std::runtime_error("Source VFX ticks must increase");
        applyRenderPending();tick=sourceTick;updated=true;for(auto& [kind,particles]:active){(void)kind;for(auto& p:particles)p.deferredDraw=false;}
        // Native411c31: modifier nodes in ascending group order, then40f880 bookkeeping.
        for(const auto& [kind,d]:definitions){auto& particles=active[kind];
            if(d.frameMode) {auto& timer=frameTimers.at(kind);if(timer)--timer;
                else {timer=d.frameDelay;for(auto& p:particles)p.tile=(p.tile+1)%(d.columns*d.rows);}}
            edparticle::PhysicalActors repellers{};
            if(kind==8&&!particles.empty()) {
                if(!host.repellingActors)throw std::runtime_error("Source shell actor-repulsion capability is missing");
                repellers=host.repellingActors();
            }
            for(auto& p:particles){
                for(unsigned axis=0;axis<3;++axis)p.velocity[axis]+=d.acceleration[axis];
                if(kind==8)p.velocity=edparticle::repel(p.position,p.velocity,repellers);
                for(unsigned axis=0;axis<3;++axis)p.rotation[axis]=uint8_t(p.rotation[axis]+d.rotationStep[axis]);
                if(!p.stopped)for(unsigned axis=0;axis<3;++axis)p.position[axis]+=p.velocity[axis];
            }
            if(d.collision)for(auto& p:particles)collide(p,d);
            // Native410650: oldage16 jitter follows floor collision, before life/children.
            if(d.jitterMinAge)for(auto& p:particles)if(p.age>=int(d.jitterMinAge))for(unsigned axis=0;axis<3;++axis) {
                constexpr float inverse32767=3.0518509447574615e-05f;const float half=float(double(d.positionJitter[axis])*.5);
                p.position[axis]=float(double(random(false,32767))*inverse32767*d.positionJitter[axis]-half+p.position[axis]);
            }
            if(d.life)for(auto& p:particles)if(p.age>int(d.life))p.age=-1;
            for(const auto& child:d.children)for(const auto& p:particles) {
                if(child.flags&1){if(p.age==-1)emit(child.kind,p.position,p.position,0);}
                else if((child.flags&4)&&unsigned(random(true,100))<child.chance)emit(child.kind,p.position,p.position,0);
            }
            // Native4112D0 visits each particle once: death has priority over
            // random child. Splitting these into two loops changes shared RNG order.
            if(d.childDeath||d.randomChildChance)for(const auto& p:particles) {
                if(d.childDeath&&p.age==-1)emit(d.childDeath,p.position,p.position,0);
                else if(d.randomChildChance&&unsigned(random(true,100))<d.randomChildChance)emit(d.randomChild,p.position,p.position,0);
            }
            if(d.periodicChild)for(const auto& p:particles){auto& timer=childTimers.at(kind);
                if(timer){--timer;continue;}timer=d.childPeriod;
                if(p.age<=int(d.childMaxAge))emit(d.periodicChild,p.position,p.position,0);}
        }
        for(auto& [kind,particles]:active){const auto& d=definitions.at(kind);
            particles.erase(std::remove_if(particles.begin(),particles.end(),[](const auto& p){return p.age<0;}),particles.end());
            for(auto& p:particles){for(unsigned c=0;c<4;++c)p.color[c]=uint8_t(std::clamp(int(p.color[c])+int(d.delta[c]),0,255));
                p.age=p.age==32767?-32768:p.age+1;p.size+=d.growth;finite(p.position);finite(p.velocity);finite(p.size);}}
    }
    void sampleRender(bool renderOnlyAdvance=false){
        if(!treeEffects||(!renderOnlyAdvance&&renderSampled&&sampledTick==tick))return;
        if(!host.visible)throw std::runtime_error("Source VFX category8 visibility capability is missing");
        if(renderOnlyAdvance){applyRenderPending();for(auto& [kind,particles]:active){(void)kind;for(auto& p:particles)p.deferredDraw=false;}}
        renderSampled=true;sampledTick=tick;
        auto child=[&](uint32_t kind,Vector position){const auto first=nextSerial;const bool result=emit(kind,position,position,0);
            if(kind<24)for(auto& p:active[kind])if(p.serial>=first)p.deferredDraw=true;return result;};
        for(const auto& p:active[24])if(host.visible(p.position)) {
            if(p.age%16!=0)child(22,p.position);
            if(p.renderMode!=1)continue;
            if(!variantEffects||!host.actorWorld)throw std::runtime_error("Source corona player-position capability is missing");
            const auto player=host.actorWorld(0,0);finite(player);const float dx=float(double(player[0])-p.position[0]),dz=float(double(player[2])-p.position[2]);
            auto after=p.position;after[0]=float(double(after[0])+double(dx)*.25);after[2]=float(double(after[2])+double(dz)*.25);
            if(!(p.age&1)){child(0,after);child(3,after);child(32,after);}else{child(3,after);child(3,after);child(3,after);}
            renderPending[p.serial]={after,std::abs(dx)<.5f&&std::abs(dz)<.5f?-1:p.age};
        }
    }
    std::vector<Batch> draw(Vector right,Vector up,Vector cameraPosition,float nativeProjectionYScale=0)const{
        finite(right);finite(up);finite(cameraPosition);finite(nativeProjectionYScale);
        if(nativeProjectionYScale<0)throw std::runtime_error("Invalid native VFX projection scale");
        Vector forward{};
        if(nativeProjectionYScale>0){
            forward={{right[1]*up[2]-right[2]*up[1],right[2]*up[0]-right[0]*up[2],right[0]*up[1]-right[1]*up[0]}};
            const float size=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
            float right2=0,up2=0,alignment=0;for(unsigned axis=0;axis<3;++axis){right2+=right[axis]*right[axis];up2+=up[axis]*up[axis];alignment+=right[axis]*up[axis];}
            if(size<.999f||size>1.001f||std::abs(right2-1)>.001f||std::abs(up2-1)>.001f||std::abs(alignment)>.001f)
                throw std::runtime_error("Native VFX expects orthonormal camera axes");
            for(auto& value:forward)value/=size;
        }
        if(!host.visible)throw std::runtime_error("Source VFX category8 visibility capability is missing");std::vector<Batch> result;
        for(const auto& [kind,particles]:active){const auto& d=definitions.at(kind);float decalOffset=0;for(const auto& p:particles){
            if(p.deferredDraw||!host.visible(p.position))continue;if(kind==25){result.push_back(ring(p,d));continue;}
            auto batch=makeBatch(kind);for(unsigned c=0;c<4;++c)batch.tint[c]=float(p.color[c])/255;
            const float du=(d.uv[2]-d.uv[0])/d.columns,dv=(d.uv[3]-d.uv[1])/d.rows;
            const float u=d.uv[0]+float(p.tile%d.columns)*du,v=d.uv[1]+float(p.tile/d.columns)*dv;
            if(kind==24) {
                const auto& mode=coronaModes.at(p.renderMode);
                const float size=float(double(mode.size[0])+double(p.age)*mode.size[1]*.125);
                for(unsigned pass=0;pass<2;++pass){auto layer=makeBatch(kind);
                    layer.tint=pass?std::array<float,4>{{1,1,128.f/255,64.f/255}}:batch.tint;
                    std::array<edmesh::Vertex32,4> quad{};
                    for(unsigned corner=0;corner<4;++corner){const float x=(corner==1||corner==2)?1.f:-1.f,y=corner>=2?1.f:-1.f;
                        auto& out=quad[corner];out.x=p.position[0]+(right[0]*x+up[0]*y)*size;
                        out.y=p.position[1]+(right[1]*x+up[1]*y)*size;out.z=p.position[2]+(right[2]*x+up[2]*y)*size;
                        if(!pass&&mode.hemisphere&&corner>=2)out.y=p.position[1]+right[1]*size;
                        out.u=pass?(x>0?.5f:.375f):u+(x>0?du:0);
                        out.v=pass?(y>0?.875f:.75f):v+(y>0?dv*(mode.hemisphere?.5f:1.f):0);}
                    for(unsigned index:{0u,1u,2u,0u,2u,3u})layer.vertices.push_back(quad[index]);result.push_back(std::move(layer));}
                auto after=p;if(const auto at=renderPending.find(p.serial);at!=renderPending.end()){after.position=at->second.position;after.age=at->second.age;}
                result.push_back(coronaSecondary(after,p.age));continue;
            }
            if(kind==7||d.nativeRenderer==0x8c08caa0){ // DC8c08caa0 emits one3-vertex strip; the legacy7 profile has crossed triangles.
                const bool nativeTriangle=d.nativeRenderer==0x8c08caa0;
                for(unsigned vertex=0;vertex<(nativeTriangle?3u:6u);++vertex){edmesh::Vertex32 out{};const unsigned corner=vertex%3;
                    out.x=p.position[0];out.y=p.position[1];out.z=p.position[2];
                    if(corner==1){out.x-=p.velocity[0];out.y-=p.velocity[1];out.z-=p.velocity[2];}
                    else if(corner==2){const float epsilon=nativeTriangle?.009999999776482582f:vertex<3?.019999999552965164f:-.019999999552965164f;out.x+=epsilon;out.y+=epsilon;if(!nativeTriangle)out.z+=epsilon;}
                    out.u=corner?u+du:u;out.v=corner==2?v+dv:v;batch.vertices.push_back(out);}
                result.push_back(std::move(batch));continue;}
            auto center=p.position;
            if(kind==23&&!(d.nativeRenderer==0x8c090920&&nativeProjectionYScale>0)){ // Prior three-argument/legacy contract.
                Vector direction{};double squared=0;for(unsigned axis=0;axis<3;++axis){direction[axis]=cameraPosition[axis]-center[axis];squared+=double(direction[axis])*direction[axis];}
                const double length=squared==0?1:std::sqrt(squared);
                for(unsigned axis=0;axis<3;++axis)center[axis]+=float(double(direction[axis])/length)*.5f;
            }
            if(kind==22){if(!host.coarseHeight)throw std::runtime_error("Source ranged trail float-grid capability is missing");
                center[1]=host.coarseHeight(center[0],center[2]);finite(center);}
            float size=(kind==17||kind==18||kind==22)?p.size:p.size*.0010000000474974513f;
            if((d.nativeRenderer==0x8c0904c0||d.nativeRenderer==0x8c090920)&&nativeProjectionYScale>0){
                // SH4 09065c..6ca /090abc..b2a clamp native1/Z before screen projection.
                float viewZ=0;for(unsigned axis=0;axis<3;++axis)viewZ+=(center[axis]-cameraPosition[axis])*forward[axis];
                const float nativeZ=viewZ<1?1:viewZ*1.0000200271606445f;
                for(unsigned axis=0;axis<3;++axis)center[axis]+=forward[axis]*(nativeZ-viewZ);
                if(d.nativeRenderer==0x8c090920){
                    // SH4 090b2c..b70: unbiased screenXY, then vertex inverseZ+.04. Scaling the ray preserves screenXY.
                    const float inverse=1/nativeZ,biased=inverse+.03999999910593033f,ratio=inverse/biased;
                    for(unsigned axis=0;axis<3;++axis)center[axis]=cameraPosition[axis]+(center[axis]-cameraPosition[axis])*ratio;
                    if(biased>1){ // Native090b74..78 writes screen centerX=1; retain this close-range branch.
                        float x=0;for(unsigned axis=0;axis<3;++axis)x+=(center[axis]-cameraPosition[axis])*right[axis];
                        const float nativeX=-319/(240*nativeProjectionYScale*biased);
                        for(unsigned axis=0;axis<3;++axis)center[axis]+=right[axis]*(nativeX-x);
                    }
                }
                // SH4 090832..84c /090cb2..ccc: pixel halfextent=size*emittedInverseZ*.5.
                size=p.size/(480*nativeProjectionYScale);
            }
            // Keep blood's bilinear taps inside its atlas cell, away from the adjacent white flash.
            const float uvInset=kind==18&&d.nativeRenderer==0x8c08dda0?.5f/256:0;
            std::array<edmesh::Vertex32,4> quad{};for(unsigned corner=0;corner<4;++corner){const float x=(corner==1||corner==2)?1.f:-1.f,y=corner>=2?1.f:-1.f;
                auto& vertex=quad[corner];
                if(kind==17||kind==18||kind==22) {vertex.x=center[0]+x*size;vertex.y=center[1]+decalOffset;vertex.z=center[2]+y*size;}
                else if(kind==19) {
                    constexpr float degrees=.01745329238474369f;Vector angles{};
                    for(unsigned axis=0;axis<3;++axis)angles[axis]=float(double(p.rotation[axis])*degrees*1.40625);
                    const auto local=edactormath::modelTransform(angles,{{p.size*.25f*x,p.size*.5f*y,0}});
                    vertex.x=center[0]+local[0];vertex.y=center[1]+local[1];vertex.z=center[2]+local[2];
                } else {vertex.x=center[0]+(right[0]*x+up[0]*y)*size;vertex.y=center[1]+(right[1]*x+up[1]*y)*size;
                    vertex.z=center[2]+(right[2]*x+up[2]*y)*size;}
                vertex.u=u+(x>0?du-uvInset:uvInset);vertex.v=v+(y>0?dv-uvInset:uvInset);}
            if(kind==17||kind==18||kind==22)decalOffset+=.000019999999494757503f; // Native426c93/426e2b: visible flat particles only.
            for(unsigned index:{0u,1u,2u,0u,2u,3u})batch.vertices.push_back(quad[index]);result.push_back(std::move(batch));
            // Native8c0904c0 emits one sprite; PC-only secondary glows are absent from that callback.
            if((kind==6||kind==16||kind==32)&&d.nativeRenderer!=0x8c0904c0){auto glow=makeBatch(kind);glow.tint=kind==16?std::array<float,4>{{224.f/255,1,0,128.f/255}}:std::array<float,4>{{1,1,128.f/255,64.f/255}};
                const float scale=p.size*(kind==6?.0020000000949949026f:kind==16?.001500000013038516f:.003000000026077032f);
                for(unsigned corner=0;corner<4;++corner){const float x=(corner==1||corner==2)?1.f:-1.f,y=corner>=2?1.f:-1.f;auto& out=quad[corner];
                    out.x=center[0]+(right[0]*x+up[0]*y)*scale;out.y=center[1]+(right[1]*x+up[1]*y)*scale;out.z=center[2]+(right[2]*x+up[2]*y)*scale;
                    out.u=kind==16?u+(x>0?du:0)+.125f:kind==6?(x>0?.25f:.125f):(x>0?.5f:.375f);out.v=kind==16?v+(y>0?dv:0)+.125f:y>0?.875f:.75f;}
                for(unsigned index:{0u,1u,2u,0u,2u,3u})glow.vertices.push_back(quad[index]);result.push_back(std::move(glow));}
        }}
        return result;
    }
    std::vector<uint8_t> snapshot()const{
        statebytes::Writer out;out.text(suppressedEmissions?"EDVFXS6":variantEffects?"EDVFXS5":treeEffects?"EDVFXS4":childTimers.empty()?"EDVFXS2":"EDVFXS3");out.put(assetHash);out.put(tick);out.flag(updated);out.put(nextSerial);
        out.put<uint32_t>(uint32_t(frameTimers.size()));for(const auto& [kind,timer]:frameTimers){out.put(kind);out.put(timer);}
        if(!childTimers.empty()){out.put<uint32_t>(uint32_t(childTimers.size()));for(const auto& [kind,timer]:childTimers){out.put(kind);out.put(timer);}}
        if(treeEffects){out.put<uint32_t>(uint32_t(emitterModes.size()));for(const auto& [kind,mode]:emitterModes){out.put(kind);out.put(mode);}out.flag(renderSampled);out.put(sampledTick);}
        out.put<uint32_t>(uint32_t(count()));
        for(const auto& [kind,particles]:active)for(const auto& p:particles){out.put(kind);out.put(p.serial);for(auto n:p.position)out.put(n);for(auto n:p.velocity)out.put(n);
            out.put(p.size);out.put(p.age);out.put(p.tile);for(auto c:p.color)out.put(c);for(auto r:p.rotation)out.put(r);out.flag(p.stopped);if(treeEffects){out.put(p.renderMode);out.flag(p.deferredDraw);}}
        if(variantEffects){out.put<uint32_t>(uint32_t(renderPending.size()));for(const auto& [serial,mutation]:renderPending){out.put(serial);for(auto value:mutation.position)out.put(value);out.put(mutation.age);}}
        if(suppressedEmissions)out.put(suppressedEmissions);
        return out.bytes;
    }
    void restore(const std::vector<uint8_t>& bytes){
        if(bytes.size()>128*1024)throw std::runtime_error("Excessive source VFX snapshot");statebytes::Reader input{bytes};
        const auto magic=input.text();if((magic!="EDVFXS1"&&magic!="EDVFXS2"&&magic!="EDVFXS3"&&magic!="EDVFXS4"&&magic!="EDVFXS5"&&magic!="EDVFXS6")||input.get<uint32_t>()!=assetHash)throw std::runtime_error("Source VFX snapshot definition mismatch");
        if(magic!="EDVFXS6"&&(((magic=="EDVFXS4"||magic=="EDVFXS5")!=treeEffects)||(magic=="EDVFXS5")!=variantEffects))throw std::runtime_error("Saved VFX render state differs from assets");
        const auto sourceTick=input.get<uint64_t>();const auto wasUpdated=input.flag();const auto serial=input.get<uint64_t>();
        if(wasUpdated!=(sourceTick!=0)||!serial)throw std::runtime_error("Invalid saved VFX clock");
        auto timers=frameTimers;if(magic!="EDVFXS1") {
            if(input.count(35)!=timers.size())throw std::runtime_error("Saved VFX frame timer count mismatch");std::set<uint32_t> seen;
            for(unsigned i=0;i<timers.size();++i) {const auto kind=input.get<uint32_t>(),timer=input.get<uint32_t>();
                if(!timers.count(kind)||!seen.insert(kind).second||timer>definitions.at(kind).frameDelay)throw std::runtime_error("Invalid saved VFX frame timer");timers[kind]=timer;}
        } else if(!timers.empty())throw std::runtime_error("Legacy VFX snapshot lacks source frame timers");
        auto children=childTimers;if(magic=="EDVFXS3"||magic=="EDVFXS4"||magic=="EDVFXS5"||(magic=="EDVFXS6"&&!children.empty())){
            if(input.count(35)!=children.size())throw std::runtime_error("Saved periodic VFX timer count mismatch");std::set<uint32_t> seen;
            for(unsigned i=0;i<children.size();++i){const auto kind=input.get<uint32_t>(),timer=input.get<uint32_t>();
                if(!children.count(kind)||!seen.insert(kind).second||timer>definitions.at(kind).childPeriod)throw std::runtime_error("Invalid saved periodic VFX timer");children[kind]=timer;}
        }else if(!children.empty())throw std::runtime_error("Legacy VFX snapshot lacks periodic child timers");
        auto modes=emitterModes;bool sampled=false;uint64_t sampleTick=0;
        if(treeEffects){if(input.count(3)!=3)throw std::runtime_error("Saved VFX emitter mode count mismatch");std::set<uint32_t> seen;
            for(unsigned n=0;n<3;++n){const auto kind=input.get<uint32_t>();const auto mode=input.get<int32_t>();
                if(!modes.count(kind)||!seen.insert(kind).second||mode<0||(kind==15&&mode>2)||(kind==24&&mode>6)||(kind==25&&mode>13))
                    throw std::runtime_error("Invalid saved source VFX emitter mode");modes[kind]=mode;}
            sampled=input.flag();sampleTick=input.get<uint64_t>();if(sampleTick>sourceTick)throw std::runtime_error("Invalid saved VFX render phase");}
        std::map<uint32_t,std::vector<Particle>> candidate;std::set<uint64_t> ids;const auto particles=input.count(pool);
        for(unsigned i=0;i<particles;++i){Particle p;p.kind=input.get<uint32_t>();p.serial=input.get<uint64_t>();for(auto& n:p.position)n=input.get<float>();for(auto& n:p.velocity)n=input.get<float>();
            p.size=input.get<float>();p.age=input.get<int32_t>();p.tile=input.get<uint32_t>();for(auto& c:p.color)c=input.get<uint8_t>();
            if(magic!="EDVFXS1"){for(auto& r:p.rotation)r=input.get<uint8_t>();p.stopped=input.flag();}if(treeEffects){p.renderMode=input.get<int32_t>();p.deferredDraw=input.flag();}
            if(!supports(p.kind)||!p.serial||p.serial>=serial||!ids.insert(p.serial).second)throw std::runtime_error("Invalid saved VFX identity");
            const auto& d=definitions.at(p.kind);if(!treeEffects)p.renderMode=int32_t(d.requiredMode);
            if((p.kind==15?(p.renderMode<0||p.renderMode>2):p.kind==24?!coronaModes.count(p.renderMode):p.kind==25&&treeEffects?!ringModes.count(uint32_t(p.renderMode)):p.renderMode!=int32_t(d.requiredMode))||
                (p.deferredDraw&&((p.kind!=22&&!(variantEffects&&(p.kind==0||p.kind==3)))||!sampled||sampleTick!=sourceTick)))throw std::runtime_error("Invalid saved VFX render variant");
            const bool renderKilled=variantEffects&&p.kind==24&&p.renderMode==1&&p.age==-1;
            // Native16 keeps size-1 atliveage61 for one frame before lifetime removal.
            if((d.life?((p.age<0&&!renderKilled)||p.age>int(d.life)+1):(p.age<0&&p.age!=-32768))||p.age>32767||
                p.tile>=d.columns*d.rows||(p.size<0&&!(p.kind==16&&p.age==61&&p.size==-1))||(p.stopped&&(d.collision!=2||d.bounceFlags)))throw std::runtime_error("Invalid saved VFX particle timeline");
            if((p.kind!=19&&p.kind!=17&&p.rotation!=std::array<uint8_t,3>{})||
               (p.kind==17&&(p.rotation[0]||p.rotation[2]||(p.rotation[1]&15))))throw std::runtime_error("Invalid saved VFX rotation");
            const auto maximumEmission=d.countMax==d.countMin?d.countMax:d.countMax-1;
            auto& group=candidate[p.kind];if((d.cap>=0&&group.size()>=unsigned(d.cap)+maximumEmission-1)||(!group.empty()&&p.serial>=group.back().serial))throw std::runtime_error("Invalid saved VFX group order/capacity");group.push_back(p);}
        std::map<uint64_t,RenderMutation> pending;
        if(variantEffects){const auto mutations=input.count(pool);
            if(mutations&&(!sampled||sampleTick!=sourceTick))throw std::runtime_error("Invalid saved VFX pending render phase");
            for(unsigned n=0;n<mutations;++n){const auto identity=input.get<uint64_t>();RenderMutation mutation;for(auto& value:mutation.position)value=input.get<float>();mutation.age=input.get<int32_t>();
                const auto group=candidate.find(24);if(group==candidate.end())throw std::runtime_error("Saved corona mutation has no particle");
                const auto at=std::find_if(group->second.begin(),group->second.end(),[&](const Particle& p){return p.serial==identity;});
                if(at==group->second.end()||at->renderMode!=1||mutation.position[1]!=at->position[1]||
                   (mutation.age!=at->age&&mutation.age!=-1)||!pending.emplace(identity,mutation).second)
                    throw std::runtime_error("Invalid saved corona mutation");}
            for(const auto& [kind,group]:candidate)if(kind==24)for(const auto& p:group)if(p.age<0&&!pending.count(p.serial))throw std::runtime_error("Saved render-killed corona lacks mutation");
        }
        const auto suppression=magic=="EDVFXS6"?input.get<uint32_t>():0;
        if(magic=="EDVFXS6"&&(!suppression||suppression>60))throw std::runtime_error("Invalid saved portal VFX suppression");
        input.end();suppressedEmissions=suppression;active.swap(candidate);frameTimers.swap(timers);childTimers.swap(children);tick=sourceTick;updated=wasUpdated;nextSerial=serial;emitterModes.swap(modes);renderSampled=sampled;sampledTick=sampleTick;renderPending.swap(pending);
    }
};
} // namespace edvfx
