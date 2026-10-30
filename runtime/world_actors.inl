// Included inside World. Native script distances are centimetres; render and
// scene geometry use world units. Modern movement stays a separate intent.
std::vector<GroundPolygon> manualActorObstacles() const {
    std::vector<GroundPolygon> result;
    if(actors)for(const auto& [key,body]:actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&body.radius>0) {
        GroundPolygon circle;circle.points={{body.pose.x,body.pose.z}};circle.circleRadius=float(body.radius)*0.01f;
        result.push_back(std::move(circle));
    }
    return result;
}
void applyNativeActorMovement(edactors::Body& body) {
    auto displacement=edactormath::pendingToWorld(body.pendingMove,{{0,body.pose.yaw,0}},body.motionFlags);
    const Vec2 requested{displacement[0],displacement[2]};
    // Native413633..57 skips ALL collision/recovery for zero horizontal delta.
    // Pure vertical motion still applies; cached floor is a separate render phase.
    if(displacement[0]==0&&displacement[2]==0) {
        body.motionFlags2=0;body.localY+=displacement[1];body.pose.y=body.cachedFloorHeight+body.localY;return;
    }
    std::vector<GroundPolygon> obstacles;
    const auto& target=nativeScene();const unsigned filter=body.key.type==0?2:4;
    if(game.profile.dreamcast){
        const auto segment=target.queries.nativeExtentMask(body.pose.x,body.pose.z,body.pose.x+displacement[0],body.pose.z+displacement[2]);
        for(const auto& [index,object]:nativeProperties.objects)if(index>=0&&index<32&&(object.flags&1)&&!(object.flags&4)&&object.radius>0){
            const auto& p=object.position;const float radius=object.radius;
            if(!SourceSceneQueries::maskMatch(segment,target.queries.nativeExtentMask(p.x-radius,p.z-radius,p.x+radius,p.z+radius,true)))continue;
            if(edactormath::objectContact({{body.pose.x,body.pose.y,body.pose.z}},displacement,{{p.x,p.y,p.z}},float(body.radius)*.01f+radius)){
                displacement=edactormath::slideContact(displacement,{{p.x-body.pose.x,0,p.z-body.pose.z}});break;
            }
        }
    }
    for(const auto& polygon:target.ground)if(!polygon.boundaryOnly&&polygon.sourceIndex<target.queries.volumes.size()&&
        (target.queries.volumes[polygon.sourceIndex].flags&filter)) {
        obstacles.push_back(polygon);obstacles.back().contactFlags=2;
    }
    for(const auto& [key,other]:actors->bodies())if(other.nativeSlot>=0&&!(key==body.key)&&(other.flags&1)&&!(other.flags&8)&&other.radius>0) {
        const float ownRadius=body.key.type==0?target.radius:float(body.radius)*.01f;
        const float otherRadius=key.type==0?target.radius:float(other.radius)*.01f;
        GroundPolygon circle;circle.points={{other.pose.x,other.pose.z}};circle.circleRadius=ownRadius+otherRadius;
        circle.contactFlags=4;
        obstacles.push_back(std::move(circle));
    }
    unsigned contacts=0;
    const Vec2 old{body.pose.x,body.pose.z},delta{displacement[0],displacement[2]};
    const auto first=game.profile.dreamcast?sweepGround(obstacles,old,delta,0):GroundHit{};
    Vec2 next;
    if(first.found&&first.contactFlags==2&&clearGround(obstacles,old,0)) {
        // Native05e360 retries the full world delta once, without advancing to the first hit.
        displacement=edactormath::slideContact(displacement,{{first.normal.x,0,first.normal.z}});
        const Vec2 retry{displacement[0],displacement[2]};const auto second=sweepGround(obstacles,old,retry,0);
        contacts=second.found?second.contactFlags:0;
        next=second.found?old:old+retry;
        if(second.found)displacement={{0,0,0}};
    } else {
        // shortcut: initial overlaps and actor-only contacts retain swept recovery; native query fidelity remains separate.
        next=displacement[0]==0&&displacement[2]==0?old:moveGround(obstacles,old,delta,0,&contacts);
    }
    if(body.key.type==0&&game.profile.dreamcast)nativeMovementStep={length(requested),length(next-Vec2{body.pose.x,body.pose.z}),requested};
    body.motionFlags2=uint8_t(contacts);
    body.pose.x=next.x;body.pose.z=next.z;body.localY+=displacement[1];
    body.pose.y=body.cachedFloorHeight+body.localY;
}
void initializeActors() {
    edactors::Host host;
    host.playerDamageAllowed=[this]{return !debugSettings||!debugSettings->invulnerable;};
    host.spawnPose=[this](edactors::Key key,int,int,int){if(routes)routes->actorCreated(key);return edactors::Pose{};};
    host.floorHeight=[this](float x,float z){return floorHeight(nativeScene().floor,{x,z}).value_or(0);};
    host.floorHeightForActor=[this](edactors::Key,float x,float z,int32_t& cache){
        return float(nativeScene().sourceFloor.actorHeight(x,0,z,cache,sourceFloorCache));
    };
    host.random=[this](int bound){return nativeProperties.randomBound(bound);};
    host.turnAmount=[this]{return nativeProperties.turnAmount;};
    host.soundMaterials=[this](const edactors::Body& body){
        nativeProperties.soundListener={body.pose.x,body.localY,body.pose.z};
        return edanimsound::surfaceCodes(nativeScene().queries,{body.pose.x,body.localY,body.pose.z},nativeScene().enabledVolumes);};
    host.soundFrame=[this]{processSourceAudio();};
    host.pickupObjects=[this] {
        std::vector<edactors::PickupObject> result;
        for(const auto& [index,object]:nativeProperties.objects)
            result.push_back({index,(object.flags&1)!=0,object.position.x,object.position.z,object.setup,object.position.y});
        return result;
    };
    host.rootMotionAllowed=[this](edactors::Key key,const edactions::State& state) {
        if(game.profile.dreamcast)return true;
        if(key.type!=0)return true;
        const auto& states=actors->at(key).actions.states;
        return state.animation!=states.at(11).animation&&state.animation!=states.at(13).animation;
    };
    host.visibleArea=[this](edactors::Key key){const auto& body=actors->at(key);
        return visibilityReady&&nativeScene().queries.visible(2,sourceQueryCamera(),{body.pose.x,body.pose.y,body.pose.z});};
    host.lineOfSight=[this](edactors::Key first,edactors::Key second){
        const auto& a=actors->at(first);const auto& b=actors->at(second);
        return nativeScene().queries.lineOfSight({a.pose.x,a.pose.y,a.pose.z},{b.pose.x,b.localY,b.pose.z},
            first.type==0?4:2,sourceQueryCamera(),nativeScene().enabledVolumes,visibilityReady);
    };
    host.lineOfSightDirection=[this](edactors::Key key,int first,int second){
        const auto& body=actors->at(key);
        const auto target=edactormath::losDirectionTarget({{body.pose.x,body.localY,body.pose.z}},first,second,{{0,body.pose.yaw,0}},body.motionFlags);
        return nativeScene().queries.lineOfSight({body.pose.x,body.pose.y,body.pose.z},
            {target[0],target[1],target[2]},key.type==0?4:2,
            sourceQueryCamera(),nativeScene().enabledVolumes,visibilityReady);
    };
    host.facing=[this](edactors::Key first,edactors::Key second,int degrees){
        const auto& a=actors->at(first);const auto& b=actors->at(second);
        Door target;target.marker={b.pose.x,b.localY,b.pose.z};target.facingArg=float(degrees);
        return facingDoor(target,{a.pose.x,a.pose.z},a.pose.yaw);
    };
    host.move=[](edactors::Body& body,int forward,int strafe,int vertical) {
        edactormath::appendMovement(body.pendingMove,forward,strafe,vertical,{{0,body.pose.yaw,0}},body.motionFlags);return 1;
    };
    host.applyMovement=[this](edactors::Body& body){applyNativeActorMovement(body);};
    host.effect=[this](const std::string& name,const std::vector<int>& args){
        queueEffect(name,std::vector<int32_t>(args.begin(),args.end()));return 0;
    };
    host.animationSound=[this](const edactors::Body& body,const std::array<uint8_t,20>& raw){
        const auto sound=edanimevent::decodeSound(raw);
        const int clip=audioBank.resolve(nativeScene().id,sound.resource,levelBPackage);
        if(body.key.type==0) {
            const auto values=edanimsound::ashArguments(sound,body.key.index,clip,body.sfxMaterial);std::vector<jpc::Value> args;
            for(int value:values)args.push_back(jpc::Value::integer(value));
            if(game.vm.invoke(game.package.methodIndex("ActorDB","processSFXAction","(IIIIIIIIIII)I"),0,std::move(args)).integer())return;
        }
        const int token=audioBank.playAtFrequency(clip,100,sound.frequencyHz());
        if(token>=0){auto& source=nativeProperties.voices[token];source.flags=0x404;
            source.position={body.pose.x,body.localY,body.pose.z};source.positionKnown=true;
            if(preparingScene)preparedVoices.push_back(token);}
    };
    host.animationEvent=[this](const edactors::Body& body,const std::array<uint8_t,36>& raw){
        const auto event=edanimevent::decode(raw);
        if(event.kind==35) {
            const auto rumble=edanimevent::rumble35(event,body.key.type);std::vector<jpc::Value> args;
            for(int value:rumble)args.push_back(jpc::Value::integer(value));
            game.vm.invoke(game.package.methodIndex("RumbleCommander","engineReportRumbleVFX","(IIII)V"),0,std::move(args));
        }
        if(edanimevent::scriptVfxCallback(event.kind)) {
            std::vector<jpc::Value> args(11,jpc::Value::integer(0));
            args[0]=jpc::Value::integer(body.key.type);args[1]=jpc::Value::integer(body.key.index);args[2]=jpc::Value::integer(event.kind);
            for(unsigned i=0;i<4;++i)args[3+i]=jpc::Value::integer(body.sfxMaterial[i]);
            if(game.vm.invoke(game.package.methodIndex("ActorDB","processVFXAction","(IIIIIIIIIII)I"),0,std::move(args)).integer())return;
        }
        if(edanimevent::skipsVisual(event.kind))return;
        const auto line=edanimevent::visualEndpoints(event,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});
        vfx->line(event.kind,line.first,line.second,event.mode);
        if(body.key.type==0&&event.kind==7) {
            std::vector<edfirearm::Actor> candidates;
            for(const auto& [key,target]:actors->bodies())candidates.push_back({key.type,key.index,target.nativeSlot,uint32_t(target.flags),
                {{target.pose.x,target.localY,target.pose.z}},target.pose.yaw,target.radius});
            const edfirearm::Actor shooter{body.key.type,body.key.index,body.nativeSlot,uint32_t(body.flags),
                {{body.pose.x,body.localY,body.pose.z}},body.pose.yaw,body.radius};
            const auto sight=[this](const edfirearm::Actor& from,const edfirearm::Actor& to){return nativeScene().queries.lineOfSight(
                {from.local[0],from.local[1],from.local[2]},{to.local[0],to.local[1],to.local[2]},4,sourceQueryCamera(),nativeScene().enabledVolumes,false);};
            for(const auto& hit:edfirearm::scan(shooter,candidates,sight))actors->post({{hit.targetType,hit.targetIndex},
                {hit.sourceType,hit.sourceIndex},hit.event,{{hit.selector,0,0,0}}});
        }
    };
    actors=std::make_unique<edactors::GameplayActors>(game,root,std::move(host));
}
void initializeRoutes() {
    SourceActorRoutes::Host host;
    host.camera=[this]{return sourceQueryCamera();};
    host.visibilityReady=[this]{return visibilityReady;};
    host.enabledVolumes=[this]()->const std::vector<bool>&{return nativeScene().enabledVolumes;};
    host.objectPosition=[this](int index){
        const auto found=nativeProperties.objects.find(index);
        if(found==nativeProperties.objects.end())throw jpc::Error("Source route object is missing");return found->second.position;
    };
    host.collisionStatus=[this](edactors::Key key){return actors->at(key).motionFlags2;};
    host.random=[this](int bound){return nativeProperties.randomBound(bound);};
    routes=std::make_unique<SourceActorRoutes>(game.vm,*actors,scene.nativeBytes,std::move(host));routes->bind();
}
GroundPoint scriptActorPosition(const std::vector<jpc::Value>& args) const {
    if(args.size()<2)throw jpc::Error("Missing native actor key");
    if(actors) {const auto& body=actors->at({args[0].integer(),args[1].integer()});return {body.pose.x,body.localY,body.pose.z};}
    requireAsh(args);const auto& placed=scriptActor?*scriptActor:actor;
    return {placed.position[0],placed.floorOffset,placed.position[2]};
}
float scriptActorYaw(const std::vector<jpc::Value>& args) const {
    if(actors)return actors->at({args[0].integer(),args[1].integer()}).pose.yaw;
    requireAsh(args);return (scriptActor?*scriptActor:actor).yaw;
}
void placeNativePlayer(const Actor& placed) {
    if(!actors)return;
    auto& body=actors->at({0,0});body.localY=placed.floorOffset;
    body.pose={placed.position[0],body.cachedFloorHeight+body.localY,placed.position[2],placed.yaw};
}
void readNativePlayer(Actor& placed,const Scene& target,bool constrainClearance=true) {
    if(!actors)return;
    const auto& body=actors->at({0,0});placed.position[0]=body.pose.x;placed.position[1]=body.pose.y;placed.position[2]=body.pose.z;
    placed.floorOffset=body.localY;placed.yaw=body.pose.yaw;
    if(game.profile.dreamcast||!constrainClearance||target.menuContext||!clearGround(target.ground,{body.pose.x,body.pose.z},0))placed.clearanceRadius=0;
    else if(placed.clearanceRadius<0||!clearGround(target.ground,{body.pose.x,body.pose.z},placed.clearanceRadius))
        placed.clearanceRadius=initialActorRadius(target,{body.pose.x,body.pose.z});
    if(!floorHeight(target.floor,{body.pose.x,body.pose.z},&placed.floorTriangle))placed.floorTriangle=std::numeric_limits<unsigned>::max();
}
