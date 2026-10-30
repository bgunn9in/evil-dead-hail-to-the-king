#pragma once
#include "gameplay_actors.h"
#include "source_scene_queries.h"
#include "source_actor_math.h"

// Native memory/route state is separate from animation and modern movement.
class SourceActorRoutes {
public:
    using Key=edactors::Key;
    struct Host {
        std::function<unsigned()> camera;
        std::function<const std::vector<bool>&()> enabledVolumes;
        std::function<GroundPoint(int)> objectPosition;
        std::function<unsigned(Key)> collisionStatus; // AuxE9, distinct from motion configuration byteE8.
        std::function<int(int)> random;
        std::function<bool()> visibilityReady;
    };
private:
    struct Memory {std::array<GroundPoint,5> spots{};std::array<bool,5> recorded{};int route=-1,cursor=0,position=0;};
    struct Route {
        int status=0,collisionClass=2,kind=0,targetType=0,targetIndex=0,startNav=-1,endNav=-1,job=-1;
        bool direct=false,reserved=false;
        GroundPoint start{},end{},cached{};
        std::vector<int> path;
    };
    struct Link {unsigned from=0,to=0;float cost=0;uint8_t flags=0,uses=0,initialFlags=0,initialUses=0;};
    struct SearchNode {float total=0,cost=0,heuristic=0;int parent=-1;};
    struct Search {
        int status=0,start=-1,goal=-1,waiting=-1,last=-1;
        std::vector<int> open,closed;
        std::vector<SearchNode> nodes;
    };
    jpc::Interpreter& vm;edactors::GameplayActors& actors;Host host;
    ScriptScene scene;SourceSceneQueries queries;
    std::vector<GroundPoint> navigation;
    std::vector<Link> links;
    std::vector<std::pair<unsigned,unsigned>> adjacency;
    std::map<Key,Memory> memory;
    std::array<Route,16> routes{};
    std::array<Search,4> searches{};
    uint32_t sceneHash=0;
    static jpc::Value I(int32_t n){return jpc::Value::integer(n);}
    static Key key(const std::vector<jpc::Value>& args){return {args[0].integer(),args[1].integer()};}
    bool validKey(Key k) const {return actors.supportedTypes().count(k.type)&&
        k.index>=0&&k.index<edactors::actorCapacity(k.type);}
    static void finite(GroundPoint p){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw jpc::Error("Non-finite source actor spot");}
    Memory& state(Key k){actors.at(k);return memory[k];}
    static GroundPoint local(const edactors::Body& body){return {body.pose.x,body.localY,body.pose.z};}
    static unsigned slot(int value){if(value<0||value>=5)throw jpc::Error("Actor memory spot exceeds original five slots");return unsigned(value);}
    GroundPoint point(Key k,int index) {const auto& m=state(k);const auto s=slot(index);
        if(!m.recorded[s])throw jpc::Error("Original actor memory spot has not been recorded");return m.spots[s];}
    void record(Key k,int index,GroundPoint p){finite(p);auto& m=state(k);const auto s=slot(index);m.spots[s]=p;m.recorded[s]=true;}
    const GroundPoint& nav(int index)const{if(index<0||size_t(index)>=navigation.size())throw jpc::Error("Source navigation point is missing");return navigation[size_t(index)];}
    const GroundPoint& marker(int index)const{if(index<0||size_t(index)>=scene.markers.size())throw jpc::Error("Source marker is missing");return scene.markers[size_t(index)];}
    Route& route(int index){if(index<0||index>=16)throw jpc::Error("Source route ID exceeds original sixteen slots");return routes[size_t(index)];}
    bool los(GroundPoint from,GroundPoint target,int collisionClass)const {
        if(!host.camera||!host.enabledVolumes)throw jpc::Error("Source route LOS host is missing");
        return queries.lineOfSight(from,target,collisionClass,host.camera(),host.enabledVolumes(),!host.visibilityReady||host.visibilityReady());
    }
    int nearest(GroundPoint point,int collisionClass)const {
        int found=-1;float best=0;
        for(size_t i=0;i<navigation.size();++i){const double x=double(point.x)-navigation[i].x,z=double(point.z)-navigation[i].z,squared=x*x+z*z;
            // Native4130c0 accepts point0 before applying the LOS filter to closer candidates.
            if(found<0||(squared<best&&los(navigation[i],point,collisionClass))){found=int(i);best=float(squared);}}
        return found;
    }
    void reserve(Route& r,bool enabled){if(r.reserved==enabled)return;r.reserved=enabled;
        for(size_t i=1;i<r.path.size();++i){const auto [first,count]=adjacency[size_t(r.path[i-1])];
            for(unsigned j=first;j<first+count;++j)if(links[j].to==unsigned(r.path[i])){auto& edge=links[j];
                if(enabled){++edge.uses;edge.flags|=4;}else if(--edge.uses==0&&(edge.flags&4))edge.flags^=4;break;}}}
    void destroy(Route& r){reserve(r,false);if(r.job>=0)searches[size_t(r.job)]={};r={};}
    int create(){for(unsigned i=0;i<16;++i)if(routes[i].status==0){routes[i]={};routes[i].status=1;return int(i);}return -1;}
    int request(Key source,GroundPoint target,int kind,int targetType=0,int targetIndex=0){const auto& body=actors.at(source);
        int id=-1;for(unsigned i=0;i<16;++i)if(routes[i].status==0){id=int(i);break;}if(id<0)return -1;
        Route r;r.status=1;const int requestClass=source.type==0?4:2;
        r.start={body.pose.x,body.pose.y,body.pose.z};r.end=r.cached=target;r.kind=kind;r.targetType=targetType;r.targetIndex=targetIndex;
        // Native414900 stores class2;4154b0 uses actor class only for the initial query.
        if(los(r.start,target,requestClass)){r.status=3;r.direct=true;}
        else {r.startNav=nearest(r.start,requestClass);r.endNav=nearest(target,requestClass);r.status=r.startNav<0||r.endNav<0?6:1;}
        routes[size_t(id)]=std::move(r);return id;
    }
    static double heuristic(GroundPoint point,GroundPoint start,GroundPoint goal){
        const double cross=(double(start.z)-goal.z)*(double(point.x)-goal.x)-(double(start.x)-goal.x)*(double(point.z)-goal.z);
        return std::abs(double(goal.x)-point.x)+std::abs(double(goal.z)-point.z)+std::abs(cross)*.0010000000474974513;
    }
    // shortcut: the native2000-node list pool is shared; unrelated native list users are not represented here.
    size_t listNodes()const{size_t result=0;for(const auto& r:routes)result+=r.path.size();for(const auto& s:searches)result+=s.open.size()+s.closed.size();return result;}
    void beginSearch(unsigned index){auto& s=searches[index];s.last=-1;s.open.clear();s.closed.clear();
        s.nodes.assign(navigation.size(),{});s.nodes[size_t(s.start)].heuristic=float(heuristic(nav(s.start),nav(s.start),nav(s.goal)));
        if(!std::isfinite(s.nodes[size_t(s.start)].heuristic))throw jpc::Error("Native route heuristic overflows source float storage");
        s.nodes[size_t(s.start)].total=s.nodes[size_t(s.start)].heuristic;if(listNodes()>=2000){s.status=3;return;}s.open.push_back(s.start);s.status=1;
    }
    void stepSearch(Search& s){if(s.status!=1)return;if(s.open.empty()){s.status=3;return;}
        auto best=s.open.begin();for(auto i=std::next(best);i!=s.open.end();++i)if(s.nodes[size_t(*i)].total<s.nodes[size_t(*best)].total)best=i;
        const int current=*best;s.open.erase(best);if(current==s.goal){s.last=current;s.status=2;s.open.clear();s.closed.clear();return;}
        const auto [first,count]=adjacency[size_t(current)];for(unsigned i=first;i<first+count;++i){const auto& edge=links[i];if(edge.flags)continue;
            const auto next=size_t(edge.to);const float cost=float(double(edge.cost)+s.nodes[size_t(current)].cost);
            const double h=heuristic(navigation[next],nav(s.start),nav(s.goal));const float total=float(h+cost);
            if(!std::isfinite(cost)||!std::isfinite(float(h))||!std::isfinite(total))throw jpc::Error("Native route cost overflows source float storage");
            auto open=std::find(s.open.begin(),s.open.end(),int(next));auto closed=std::find(s.closed.begin(),s.closed.end(),int(next));
            if(open!=s.open.end()){if(s.nodes[next].total<=total)continue;s.open.erase(open);}
            else if(closed!=s.closed.end()){if(s.nodes[next].total<=total)continue;s.closed.erase(closed);}
            s.nodes[next]={total,cost,float(h),current};if(listNodes()>=2000){s.status=3;s.open.clear();s.closed.clear();return;}
            s.open.insert(s.open.begin(),int(next));if(int(next)==s.goal)break;
        }if(listNodes()>=2000){s.status=3;s.open.clear();s.closed.clear();return;}s.closed.insert(s.closed.begin(),current);
    }
    void evaluate(Route& r){if(los(r.start,r.end,r.collisionClass)){r.path.clear();r.startNav=r.endNav=-1;r.status=3;return;}
        r.startNav=nearest(r.start,r.collisionClass);r.endNav=nearest(r.end,r.collisionClass);
        if(r.startNav<0||r.endNav<0){r.path.clear();r.status=6;return;}
        const auto end=std::find(r.path.begin(),r.path.end(),r.endNav);
        if(end==r.path.end()){r.path.clear();r.status=1;}else {r.path.erase(std::next(end),r.path.end());r.status=3;}
    }
    static int distance(GroundPoint a,GroundPoint b){const float x=a.x-b.x,z=a.z-b.z;const double squared=double(x)*x+double(z)*z,d=(squared==0?1:std::sqrt(squared))*100;
        if(!std::isfinite(d)||d>std::numeric_limits<int>::max())throw jpc::Error("Source spot distance exceeds native integer range");return int(d);}
    static double heading(float x,float z){double result=std::atan2(double(z),double(x));if(result<0)result+=edactormath::Tau;return result;}
    void bind(const char* name,jpc::Interpreter::Native native){const jpc::Method* selected=nullptr;
        for(const auto& method:vm.package.methods)if(vm.package.classes[size_t(method.owner)].name=="ed/base/External"&&method.name==name){
            if(selected)throw jpc::Error("Ambiguous source route native");selected=&method;}
        if(!selected)throw jpc::Error(std::string("Missing source route native: ")+name);
        vm.bind("ed/base/External",name,selected->signature,std::move(native));}
public:
    SourceActorRoutes(jpc::Interpreter& interpreter,edactors::GameplayActors& registry,const std::vector<uint8_t>& payload,Host callbacks={})
        :vm(interpreter),actors(registry),host(std::move(callbacks)){setScene(payload);}
    void setScene(const std::vector<uint8_t>& payload){
        ScriptScene next(payload);SourceSceneQueries nextQueries(payload);std::vector<GroundPoint> points;std::vector<Link> edges;std::vector<std::pair<unsigned,unsigned>> ranges;
        auto u32=[&](size_t offset){if(offset>payload.size()||payload.size()-offset<4)throw jpc::Error("Truncated original route table");
            uint32_t value;std::memcpy(&value,payload.data()+offset,4);return value;};
        const size_t count=u32(48),offset=u32(52),edgeCount=u32(56),edgeOffset=u32(60);
        if(count>150||edgeCount>100000||(count&&offset<88)||(edgeCount&&edgeOffset<88)||offset>payload.size()||count*44>payload.size()-offset||
           edgeOffset>payload.size()||edgeCount*32>payload.size()-edgeOffset)throw jpc::Error("Invalid original navigation table bounds");
        for(size_t i=0;i<count;++i){GroundPoint p;std::memcpy(&p,payload.data()+offset+i*44+8,12);finite(p);points.push_back(p);
            const auto n=u32(offset+i*44),start=u32(offset+i*44+4);if(start>edgeCount||n>edgeCount-start)throw jpc::Error("Invalid source nav adjacency range");ranges.emplace_back(start,n);}
        for(size_t i=0;i<edgeCount;++i){const auto a=u32(edgeOffset+i*32+24),b=u32(edgeOffset+i*32+28);
            if(a>=count||b>=count)throw jpc::Error("Original route link has invalid endpoint");float cost;std::memcpy(&cost,payload.data()+edgeOffset+i*32+8,4);
            if(!std::isfinite(cost)||cost<0)throw jpc::Error("Invalid original navigation edge cost");const auto flags=payload[edgeOffset+i*32],uses=payload[edgeOffset+i*32+2];
            edges.push_back({b,a,cost,flags,uses,flags,uses});}
        for(size_t i=0;i<count;++i)for(unsigned j=ranges[i].first;j<ranges[i].first+ranges[i].second;++j)
            if(edges[j].from!=i)throw jpc::Error("Source adjacency has a different originating navigation node");
        uint32_t hash=2166136261u;for(auto value:payload)hash=(hash^value)*16777619u;
        scene=std::move(next);queries=std::move(nextQueries);navigation=std::move(points);links=std::move(edges);adjacency=std::move(ranges);routes={};searches={};sceneHash=hash;
    }
    void actorCreated(Key k){if(!validKey(k))throw jpc::Error("Invalid source actor route identity");auto& m=memory[k];m.route=-1;m.cursor=m.position=0;}
    void forgetActors(const std::vector<Key>& removed){for(auto key:removed)memory.erase(key);}
    const std::vector<int>& navigationPath(int id)const{if(id<0||id>=16)throw jpc::Error("Invalid source route ID");return routes[size_t(id)].path;}
    void bind(){
        bind("actorRecordActorSpot",[this](auto&,auto,const auto& a){record(key(a),a[4].integer(),local(actors.at({a[2].integer(),a[3].integer()})));return jpc::Value{};});
        bind("actorRecordMarkerSpot",[this](auto&,auto,const auto& a){record(key(a),a[3].integer(),marker(a[2].integer()));return jpc::Value{};});
        bind("actorRecordNavPointSpot",[this](auto&,auto,const auto& a){record(key(a),a[3].integer(),nav(a[2].integer()));return jpc::Value{};});
        bind("actorRecordObjectSpot",[this](auto&,auto,const auto& a){if(!host.objectPosition)throw jpc::Error("Source object position capability is missing");record(key(a),a[3].integer(),host.objectPosition(a[2].integer()));return jpc::Value{};});
        bind("actorDistanceToSpot",[this](auto&,auto,const auto& a){return I(distance(local(actors.at(key(a))),point(key(a),a[2].integer())));});
        bind("actorDistanceToRecordedPosition",[this](auto&,auto,const auto& a){
            // Native403c50: raw five-slot memory (zero-initialized before a
            // record), local XZ; source421ba0 stores float differences.
            const auto k=key(a);return I(distance(local(actors.at(k)),state(k).spots[slot(a[2].integer())]));});
        bind("actorDistanceBetweenSpots",[this](auto&,auto,const auto& a){return I(distance(point(key(a),a[2].integer()),point(key(a),a[3].integer())));});
        bind("actorHaveLOSToSpot",[this](auto&,auto,const auto& a){if(!host.camera||!host.enabledVolumes)throw jpc::Error("Source spot LOS host is missing");
            const auto& body=actors.at(key(a));return I(queries.lineOfSight({body.pose.x,body.pose.y,body.pose.z},point(key(a),a[2].integer()),body.key.type==0?4:2,host.camera(),host.enabledVolumes(),!host.visibilityReady||host.visibilityReady()));});
        bind("sceneGetNavPointCount",[this](auto&,auto,const auto&){return I(int(navigation.size()));});
        bind("actorDistanceToNavPoint",[this](auto&,auto,const auto& a){return I(distance(local(actors.at(key(a))),nav(a[2].integer())));});
        bind("actorFaceNavPoint",[this](auto&,auto,const auto& a){auto& body=actors.at(key(a));const auto target=nav(a[2].integer());
            const float x=float(double(target.x)-body.pose.x),z=float(double(target.z)-body.pose.z);
            body.pose.yaw=edactormath::modelYawFromHeading(float(heading(x,z)));return jpc::Value{};});
        bind("actorIsFacingNavPoint",[this](auto&,auto,const auto& a){const auto& body=actors.at(key(a));const auto target=nav(a[2].integer());const int32_t degrees=a[3].integer();
            if(degrees==std::numeric_limits<int32_t>::min())return I(0);const double threshold=std::abs(degrees)*double(.01745329238474369f);
            if(threshold>=edactormath::Pi)return I(1);const float x=float(double(target.x)-body.pose.x),z=float(double(target.z)-body.pose.z);
            double delta=heading(x,z)-edactormath::nativeHeading(body.pose.yaw);if(delta<-edactormath::Pi)delta+=edactormath::Tau;if(delta>edactormath::Pi)delta-=edactormath::Tau;
            return I(std::abs(int(delta))<float(threshold));});
        bind("actorGetNearestNavPoint",[this](auto&,auto,const auto& a){const auto p=local(actors.at(key(a)));const float low=float(a[2].integer())*.01f,high=float(a[3].integer())*.01f;
            const float minimum=low*low,maximum=high*high;float best=0;int found=-1;
            for(size_t i=0;i<navigation.size();++i){const float x=p.x-navigation[i].x,z=p.z-navigation[i].z;const double squared=double(x)*x+double(z)*z;
                if(squared<minimum||squared>maximum)continue;if(found<0||squared<best){best=float(squared);found=int(i);}}
            return I(found);});
        bind("routeCreate",[this](auto&,auto,const auto&){return I(create());});
        bind("routeDestroy",[this](auto&,auto,const auto& a){const int id=a[0].integer();if(id>=0)destroy(route(id));return jpc::Value{};});
        bind("routeStatus",[this](auto&,auto,const auto& a){const int id=a[0].integer();return I(id==-1?0:route(id).status);});
        bind("routeDraw",[](auto&,auto,const auto&){return jpc::Value{};}); // Source40a7a0 is RET.
        bind("actorSetRoute",[this](auto&,auto,const auto& a){const int id=a[2].integer();if(id!=-1)route(id);auto& m=state(key(a));m.route=id;m.cursor=0;m.position=id==-1?1:2;return jpc::Value{};});
        bind("actorRouteStatus",[this](auto&,auto,const auto& a){const auto& m=state(key(a));return I(m.route==-1?0:route(m.route).status);});
        bind("actorRoutePosition",[this](auto&,auto,const auto& a){const auto& m=state(key(a));return I(m.route==-1?0:m.position);});
        bind("routeToActor",[this](auto&,auto,const auto& a){const Key target{a[2].integer(),a[3].integer()};return I(request(key(a),local(actors.at(target)),2,target.type,target.index));});
        bind("routeToPlayer",[this](auto&,auto,const auto& a){return I(request(key(a),local(actors.at({0,0})),2,0,0));});
        bind("routeToMarker",[this](auto&,auto,const auto& a){const int index=a[2].integer();return I(request(key(a),marker(index),5,0,index));});
        bind("routeToNavigationPoint",[this](auto&,auto,const auto& a){const int index=a[2].integer();return I(request(key(a),nav(index),4,0,index));});
        bind("routeToMemoryPosition",[this](auto&,auto,const auto& a){return I(request(key(a),point(key(a),a[2].integer()),1));});
    }
    // Native40ceb1 route scheduler runs after actors/VM;40cebf then advances each search once.
    void update(){for(auto& r:routes)switch(r.status){
        case 1:{if(r.startNav<0||r.endNav<0)throw jpc::Error("Source routeCreate must be configured before search");
            for(unsigned i=0;i<4;++i)if(searches[i].status==0){r.job=int(i);auto& s=searches[i];s.start=r.startNav;s.goal=r.endNav;beginSearch(i);
                if(s.status==3){s={};r.job=-1;}else r.status=2;break;}break;}
        case 2:{if(r.job<0)throw jpc::Error("Source route search job is missing");auto& s=searches[size_t(r.job)];
            if(s.status==2){r.status=3;r.path.clear();int node=s.last;for(size_t i=0;node>=0;++i){if(i>=navigation.size())throw jpc::Error("Cyclic original route search parent chain");
                    if(listNodes()<2000)r.path.insert(r.path.begin(),node);node=s.nodes[size_t(node)].parent;}s={};r.job=-1;reserve(r,true);}
            else if(s.status==3)r.status=5;else if(s.status==4)r.status=4;break;}
        case 4:case 5:{auto& s=searches.at(size_t(r.job));s.open.clear();s.closed.clear();s.status=5;
            for(unsigned i=0;i<4;++i)if(int(i)!=r.job&&searches[i].status==1){s.waiting=int(i);break;}
            if(s.waiting<0){beginSearch(unsigned(r.job));r.status=2;}else r.status=8;break;}
        case 7:evaluate(r);break;
        case 8:{auto& s=searches.at(size_t(r.job));if(s.waiting<0)throw jpc::Error("Source route retry dependency is missing");auto& other=searches.at(size_t(s.waiting));
            if(other.status==0||other.status==2){other.waiting=-1;beginSearch(unsigned(r.job));r.status=2;}break;}
        default:break;
        }for(auto& s:searches)stepSearch(s);
    }
    // Native4018b0/4016d0 precedes actor physics and the VM. Steering changes
    // heading/cursor; the original animation and actorMove still supply displacement.
    void steerActors(){std::vector<const edactors::Body*> ordered;for(const auto& [k,b]:actors.bodies())if(k.type!=0)ordered.push_back(&b);
        std::sort(ordered.begin(),ordered.end(),[](auto* a,auto* b){return a->allocationOrder<b->allocationOrder;});
        for(const auto* pointer:ordered){auto& b=actors.at(pointer->key);if(b.flags&0x48)continue;auto& m=memory[b.key];if(m.route<0)continue;
            auto& r=route(m.route);if(r.status!=3)continue;
            if(r.kind==2){const auto target=local(actors.at({r.targetType,r.targetIndex}));const float dx=float(double(r.cached.x)-target.x),dz=float(double(r.cached.z)-target.z);const double squared=double(dx)*dx+double(dz)*dz;
                if(squared>9&&squared<18){r.status=7;r.start=local(b);r.end=r.cached=target;reserve(r,false);continue;}}
            GroundPoint target;int selected=-1;
            if(r.direct){switch(r.kind){case 1:target=r.cached;break;case 2:target=local(actors.at({r.targetType,r.targetIndex}));break;
                case 4:target=nav(r.targetIndex);break;case 5:target=marker(r.targetIndex);break;default:throw jpc::Error("Unrecovered original route target kind");}}
            else {if(m.cursor){selected=m.cursor-1;nav(selected);}else if(!r.path.empty())selected=r.path.front();target=selected<0?r.end:nav(selected);}
            const float dx=float(double(target.x)-b.pose.x),dz=float(double(target.z)-b.pose.z);const double squared=double(dx)*dx+double(dz)*dz;
            if(!r.direct&&squared<=.25){if(selected>=0){if(!m.cursor)selected=r.path.empty()?-1:r.path.front();else {const auto current=std::find(r.path.begin(),r.path.end(),m.cursor-1);
                        selected=current==r.path.end()||std::next(current)==r.path.end()?-1:*std::next(current);}}
                if(selected<0){m.position=1;reserve(r,false);continue;}}
            double nativeH=heading(dx,dz);bool strafe=false;
            if(r.direct){if(!host.collisionStatus)throw jpc::Error("Source route collision status capability is missing");
                if(host.collisionStatus(b.key)&6){if(!host.random)throw jpc::Error("Source route PRNG capability is missing");const int jitter=host.random(40);
                    if(jitter<0||jitter>=40)throw jpc::Error("Original route random result is out of range");nativeH+=jitter*.01745329238474369;strafe=true;}}
            b.pose.yaw=edactormath::modelYawFromHeading(nativeH);
            if(strafe)vm.invoke(vm.package.methodIndex("ed/base/External","actorMoveStrafe","(III)I"),0,{I(b.key.type),I(b.key.index),I(20)});
            if(r.direct){if(squared<=.25){m.position=1;reserve(r,false);}}else m.cursor=selected+1;
        }
    }
    std::vector<uint8_t> snapshot()const{statebytes::Writer out;out.text("EDROUTES2");out.put(sceneHash);out.put<uint32_t>(uint32_t(memory.size()));
        auto point=[&](GroundPoint p){out.put(p.x);out.put(p.y);out.put(p.z);};
        auto nodes=[&](const std::vector<int>& values){out.put<uint32_t>(uint32_t(values.size()));for(int n:values)out.put<int32_t>(n);};
        for(const auto& [k,m]:memory){out.put<int32_t>(k.type);out.put<int32_t>(k.index);out.put<int32_t>(m.route);out.put<int32_t>(m.cursor);out.put<int32_t>(m.position);
            for(unsigned i=0;i<5;++i){out.flag(m.recorded[i]);out.put(m.spots[i].x);out.put(m.spots[i].y);out.put(m.spots[i].z);}}
        for(const auto& r:routes){for(int n:{r.status,r.collisionClass,r.kind,r.targetType,r.targetIndex,r.startNav,r.endNav,r.job})out.put<int32_t>(n);
            out.flag(r.direct);out.flag(r.reserved);point(r.start);point(r.end);point(r.cached);nodes(r.path);}
        for(const auto& s:searches){for(int n:{s.status,s.start,s.goal,s.waiting,s.last})out.put<int32_t>(n);nodes(s.open);nodes(s.closed);
            out.put<uint32_t>(uint32_t(s.nodes.size()));for(const auto& n:s.nodes){out.put(n.total);out.put(n.cost);out.put(n.heuristic);out.put<int32_t>(n.parent);}}
        out.put<uint32_t>(uint32_t(links.size()));for(const auto& e:links){out.put(e.flags);out.put(e.uses);}return out.bytes;}
    void restore(const std::vector<uint8_t>& bytes){if(bytes.size()>1024*1024)throw jpc::Error("Excessive saved source routes");statebytes::Reader in{bytes};
        if(in.text()!="EDROUTES2"||in.get<uint32_t>()!=sceneHash)throw jpc::Error("Saved source routes have different scene geometry");
        auto point=[&](){return GroundPoint{in.get<float>(),in.get<float>(),in.get<float>()};};
        auto validNav=[&](int n){return n>=0&&size_t(n)<navigation.size();};
        auto nodes=[&](){std::vector<int> result;std::set<int> unique;for(unsigned i=0,n=in.count(uint32_t(navigation.size()));i<n;++i){const int value=in.get<int32_t>();
                if(!validNav(value)||!unique.insert(value).second)throw jpc::Error("Invalid saved source nav list");result.push_back(value);}return result;};
        std::map<Key,Memory> candidate;for(unsigned i=0,n=in.count(edactors::MaxBodies);i<n;++i){const Key k{in.get<int32_t>(),in.get<int32_t>()};Memory m;
            m.route=in.get<int32_t>();m.cursor=in.get<int32_t>();m.position=in.get<int32_t>();if(!validKey(k)||m.route<-1||m.route>=16||m.cursor<0||size_t(m.cursor)>navigation.size()||m.position<0||m.position>2)throw jpc::Error("Invalid saved actor route cursor");
            for(unsigned s=0;s<5;++s){m.recorded[s]=in.flag();m.spots[s]={in.get<float>(),in.get<float>(),in.get<float>()};}
            if(!candidate.emplace(k,m).second)throw jpc::Error("Duplicate saved actor memory");}
        std::array<Route,16> pool{};std::set<int> ownedJobs;for(auto& r:pool){r.status=in.get<int32_t>();r.collisionClass=in.get<int32_t>();r.kind=in.get<int32_t>();
            r.targetType=in.get<int32_t>();r.targetIndex=in.get<int32_t>();r.startNav=in.get<int32_t>();r.endNav=in.get<int32_t>();r.job=in.get<int32_t>();
            r.direct=in.flag();r.reserved=in.flag();r.start=point();r.end=point();r.cached=point();r.path=nodes();
            if(r.status<0||r.status>8||r.kind<0||r.kind>6||r.job<-1||r.job>=4||r.startNav<-1||r.endNav<-1||
               (r.startNav>=0&&!validNav(r.startNav))||(r.endNav>=0&&!validNav(r.endNav))||
               r.collisionClass!=2)throw jpc::Error("Invalid saved source route");
            if(r.kind==3||r.kind==6||(r.kind==0&&r.status!=0&&r.status!=1)||(r.status==0&&(r.kind||r.reserved||!r.path.empty())))throw jpc::Error("Unsupported saved source route target");
            if(r.kind==2&&!validKey({r.targetType,r.targetIndex}))throw jpc::Error("Invalid saved route actor target");
            if(r.kind==4&&!validNav(r.targetIndex))throw jpc::Error("Invalid saved route navigation target");
            if(r.kind==5&&(r.targetIndex<0||size_t(r.targetIndex)>=scene.markers.size()))throw jpc::Error("Invalid saved route marker target");
            if(r.job>=0&&!ownedJobs.insert(r.job).second)throw jpc::Error("Shared saved source search job");
            const bool needsJob=r.status==2||r.status==4||r.status==5||r.status==8;
            if(needsJob!=(r.job>=0))throw jpc::Error("Saved source search ownership mismatch");
            for(size_t i=1;i<r.path.size();++i){const auto [first,count]=adjacency[size_t(r.path[i-1])];bool connected=false;
                for(unsigned j=first;j<first+count;++j)if(links[j].to==unsigned(r.path[i])){connected=true;break;}
                if(!connected)throw jpc::Error("Saved route crosses a nonexistent native edge");}}
        std::array<Search,4> jobs{};for(unsigned j=0;j<4;++j){auto& s=jobs[j];s.status=in.get<int32_t>();s.start=in.get<int32_t>();s.goal=in.get<int32_t>();s.waiting=in.get<int32_t>();s.last=in.get<int32_t>();
            s.open=nodes();s.closed=nodes();for(unsigned i=0,n=in.count(uint32_t(navigation.size()));i<n;++i){SearchNode item;
                item.total=in.get<float>();item.cost=in.get<float>();item.heuristic=in.get<float>();item.parent=in.get<int32_t>();
                if(item.parent<-1||(item.parent>=0&&!validNav(item.parent)))throw jpc::Error("Invalid saved search parent");s.nodes.push_back(item);}
            if(s.status<0||s.status>5||s.waiting<-1||s.waiting>=4||s.waiting==int(j)||s.last<-1||(s.last>=0&&!validNav(s.last))||
               (s.status&&(!validNav(s.start)||!validNav(s.goal)||s.nodes.size()!=navigation.size()))||
               (s.status==2&&s.last<0)||bool(s.status)!=bool(ownedJobs.count(int(j))))throw jpc::Error("Invalid saved source search state");
            for(int n:s.open)if(std::find(s.closed.begin(),s.closed.end(),n)!=s.closed.end())throw jpc::Error("Saved node is both open and closed");}
        for(const auto& s:jobs)for(size_t i=0;i<s.nodes.size();++i){int node=int(i);for(size_t depth=0;node>=0;++depth){
            if(depth>=s.nodes.size())throw jpc::Error("Cyclic saved source search parents");node=s.nodes[size_t(node)].parent;}}
        for(const auto& r:pool)if(r.job>=0){const auto& s=jobs[size_t(r.job)];if((r.status==2&&(s.status<1||s.status>4))||
            (r.status==4&&s.status!=4)||(r.status==5&&s.status!=3)||(r.status==8&&(s.status!=5||s.waiting<0))||s.start!=r.startNav||s.goal!=r.endNav)
                throw jpc::Error("Saved route and search state disagree");}
        if(in.count(uint32_t(links.size()))!=links.size())throw jpc::Error("Saved source edge count mismatch");auto edges=links;
        for(auto& e:edges){e.flags=in.get<uint8_t>();e.uses=in.get<uint8_t>();if((e.flags&~4)!=(e.initialFlags&~4))throw jpc::Error("Invalid saved route link flags");}
        size_t listCount=0;std::vector<unsigned> uses(links.size());for(const auto& r:pool){listCount+=r.path.size();if(r.reserved)for(size_t i=1;i<r.path.size();++i){const auto [first,count]=adjacency[size_t(r.path[i-1])];
            for(unsigned j=first;j<first+count;++j)if(links[j].to==unsigned(r.path[i])){++uses[j];break;}}}
        for(const auto& s:jobs)listCount+=s.open.size()+s.closed.size();if(listCount>2000)throw jpc::Error("Saved native linked-list pool exceeds capacity");
        for(size_t i=0;i<edges.size();++i)if(edges[i].uses!=uint8_t(unsigned(edges[i].initialUses)+uses[i])||(uses[i]&&!(edges[i].flags&4)))throw jpc::Error("Saved route edge reservations disagree");
        in.end();memory.swap(candidate);routes=std::move(pool);searches=std::move(jobs);links=std::move(edges);
    }
};
