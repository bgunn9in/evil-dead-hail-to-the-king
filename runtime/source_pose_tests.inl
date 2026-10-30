void testSourcePose(const fs::path& root) {
    Scene original=loadScene(root/"ak01");
    Actor pose=resetActor(original,0);
    float maxFloorX=0;
    for(const auto& triangle:original.floor)for(auto point:{triangle.a,triangle.b,triangle.c})maxFloorX=std::max(maxFloorX,point.x);
    pose.position[0]=maxFloorX+10;pose.position[1]=7.25f;pose.position[2]=-11;
    pose.yaw=1.234f;pose.clearanceRadius=0;pose.walking=true;
    const Actor before=pose;
    const auto idle=stepActor(original,pose,{},false,0.033f,true);
    if(std::memcmp(before.position,pose.position,sizeof(pose.position))||pose.yaw!=before.yaw||idle.moved||pose.walking||pose.camera>=original.cameras.size())
        throw std::runtime_error("Idle modern input changed the original native actor pose");

    {
        Scene facing;facing.speed=1;facing.radius=.25f;facing.cameras.resize(1);
        facing.cameras[0].right={1,0};facing.cameras[0].forward={0,1};
        facing.floor={{{-2,0,-2},{2,0,-2},{2,0,2}},{{-2,0,-2},{2,0,2},{-2,0,2}}};
        GroundPolygon crate;crate.points={{-.2f,-.2f},{.2f,-.2f},{.2f,.2f},{-.2f,.2f}};facing.ground={crate};
        GroundPolygon npc;npc.points={{0,0}};npc.circleRadius=.3f;
        Actor trapped;trapped.position[1]=7.25f;trapped.yaw=1.2f;trapped.walking=true;trapped.floorTriangle=77;trapped.animationTime=.5f;
        const auto start=trapped;const auto turn=stepActor(facing,trapped,{1,1},false,0,true,{npc});
        if(std::memcmp(start.position,trapped.position,sizeof(trapped.position))||trapped.walking||turn.requested||turn.moved||
           std::abs(trapped.yaw-std::atan2(1.f,1.f))>1e-6f||trapped.floorTriangle!=77||trapped.animationTime!=start.animationTime)
            throw std::runtime_error("Zero-time source direction recovered an authored overlap instead of only facing intent");
        auto advancing=start;const auto move=stepActor(facing,advancing,{1,1},false,.033f,true);
        if(move.requested<=0||move.moved<=0||!clearGround(facing.ground,{advancing.position[0],advancing.position[2]},0))
            throw std::runtime_error("Zero-time facing fix suppressed positive-time source recovery");
    }

    // Native42B3D0 P1 class4 tests source flag2; raw source activation is separate.
    {
        auto fixture=[](){Scene s;s.speed=1;s.radius=.25f;s.cameras.resize(1);s.cameras[0].right={1,0};s.cameras[0].forward={0,1};
            s.floor={{{-2,0,-2},{2,0,-2},{2,0,2}},{{-2,0,-2},{2,0,2},{-2,0,2}}};
            GroundPolygon wall;wall.points={{0,-1},{0,1},{.2f,1},{.2f,-1}};wall.sourceIndex=0;s.ground.push_back(wall);
            s.queries.volumes.resize(1);s.queries.volumes[0].geometry.kind=2;return s;};
        auto placed=[](){Actor a;a.position[0]=-.5f;a.clearanceRadius=.25f;return a;};
        for(uint32_t flags:{0xfffffff4u,0xfffffff8u,0u}) {
            auto s=fixture();s.queries.volumes[0].flags=flags;auto a=placed();stepActor(s,a,{1,0},false,1,true);
            if(a.position[0]<.49f||!s.ground[0].enabled||s.queries.volumes[0].flags!=flags)
                throw std::runtime_error("P1-excluded source wall blocked modern input or changed raw activation/flags");
        }
        for(uint32_t flags:{0xffffffffu,0xfffffff2u,2u}) {
            auto s=fixture();s.queries.volumes[0].flags=flags;auto a=placed();stepActor(s,a,{1,0},false,1,true);
            if(a.position[0]>.0001f||a.position[0]<-.001f)throw std::runtime_error("Modern input bypassed a true P1 source wall");
        }
        for(unsigned mode:{0u,1u,2u}) {
            auto s=fixture();s.queries.volumes[0].flags=0;auto& wall=s.ground[0];
            if(mode==0)wall.sourceIndex=std::numeric_limits<unsigned>::max();else if(mode==1)wall.sourceIndex=42;
            else s.queries.volumes[0].geometry.kind=6;
            auto a=placed();stepActor(s,a,{1,0},false,1,true);
            if(a.position[0]>.0001f)throw std::runtime_error("Source class filter removed an unknown collider");
        }
        {
            auto s=fixture();s.ground[0].boundaryOnly=true;s.ground[0].points={{0,-1},{0,1}};
            auto a=placed();stepActor(s,a,{1,0},false,1,true);
            if(a.position[0]<.49f)throw std::runtime_error("Generated internal floor seam blocked source movement");
            a=placed();stepActor(s,a,{1,0},false,1,false);
            if(a.position[0]>-.249f)throw std::runtime_error("Legacy movement lost its synthetic floor boundary");
        }
        {
            auto s=fixture();s.queries.volumes[0].flags=2;s.ground[0].enabled=false;auto a=placed();stepActor(s,a,{1,0},false,1,true);
            if(a.position[0]<.49f||s.ground[0].enabled)throw std::runtime_error("Source class filter re-enabled a disabled wall");
        }
        {
            auto s=fixture();s.queries.volumes[0].flags=0xfffffff4u;auto a=placed();stepActor(s,a,{1,0},false,1,false);
            if(a.position[0]>-.249f)throw std::runtime_error("Legacy movement unexpectedly used source P1 class filtering");
        }
        {
            auto s=fixture();s.queries.volumes[0].flags=0xfffffff4u;GroundPolygon boundary;boundary.keepInside=true;
            boundary.points={{-2,-2},{-2,2},{2,2},{2,-2}};s.ground.push_back(boundary);auto a=placed();a.position[0]=.1f;
            stepActor(s,a,{1,0},false,3,true);
            if(a.position[0]>2||!floorHeight(s.floor,{a.position[0],a.position[2]}))
                throw std::runtime_error("P1-excluded old pose bypassed filtered floor/recovery safety");
        }
    }

    {
        // Exact AC03 faces1/47 and generated edges45/93; original vertices remain untouched.
        Scene s;s.speed=1;s.cameras.resize(1);s.cameras[0].right={1,0};s.cameras[0].forward={0,1};
        s.floor={{{-33.9653015137f,0,34.765499115f},{9.69616031647f,0,-.397839009762f},{7.88274002075f,0,-3.63608002663f}},
            {{7.88274002075f,0,-3.63609004021f},{9.69616031647f,0,-.397839009762f},{8.82968044281f,-.0357402004302f,-3.73694992065f}}};
        for(Vec2 start: {Vec2{7.88274002075f,-3.63608002663f},Vec2{7.88274049759f,-3.63608908653f}}) {
            GroundPolygon edge;edge.boundaryOnly=true;edge.points={start,{9.69616031647f,-.397839009762f}};s.ground.push_back(edge);
        }
        Actor a;a.position[0]=8.97966957092f;a.position[2]=-1.67707824707f;
        const auto step=stepActor(s,a,{.0748603716493f,-.0419233180583f},false,1,true);
        if(std::abs(step.moved-step.requested)>1e-5f||!floorHeight(s.floor,{a.position[0],a.position[2]}))
            throw std::runtime_error("AC03 authored floor seam still blocks normal source input");
    }

    {
        Scene s;s.speed=1;s.cameras.resize(1);s.cameras[0].right={1,0};s.cameras[0].forward={0,1};
        s.floor={{{-2,0,-2},{2,0,-2},{2,0,2}},{{-2,0,-2},{2,0,2},{-2,0,2}}};
        for(auto points: {std::vector<Vec2>{{-2,-2},{2,-2}},{{2,-2},{2,2}},{{2,2},{-2,2}},{{-2,2},{-2,-2}}}) {
            GroundPolygon edge;edge.boundaryOnly=true;edge.points=points;s.ground.push_back(edge);
        }
        Actor a;a.position[0]=1.9f;
        stepActor(s,a,{1,1},false,.5f,true);
        if(a.position[0]>2||a.position[2]<.3f||!floorHeight(s.floor,{a.position[0],a.position[2]}))
            throw std::runtime_error("Source movement lost sliding along an actual floor edge");
        s.radius=.25f;GroundPolygon nearbyNpc;nearbyNpc.points={{1.9f,.65f}};nearbyNpc.circleRadius=.1f;
        a={};a.position[0]=1.9f;stepActor(s,a,{1,1},false,.5f,true,{nearbyNpc});
        const Vec2 point{a.position[0],a.position[2]};
        if(a.position[0]>2||a.position[2]<.15f||!floorHeight(s.floor,point)||
           length(point-nearbyNpc.points[0])<.35f-ContactSkin||nearbyNpc.circleRadius!=.1f)
            throw std::runtime_error("Floor-edge fallback changed NPC separation or applied its radius twice");
        nearbyNpc.points={{1.5f,0}};nearbyNpc.circleRadius=.4f;
        a={};a.position[0]=1.9f;const auto overlapStep=stepActor(s,a,{1,0},false,.033f,true,{nearbyNpc});
        if(a.position[0]!=1.9f||a.position[2]!=0||overlapStep.moved!=0||overlapStep.requested<=0||
           a.clearanceRadius!=0||!floorHeight(s.floor,{a.position[0],a.position[2]})||nearbyNpc.circleRadius!=.4f)
            throw std::runtime_error("Overlapping NPC recovered the player outside the original floor");
        s.floor={{{-2,0,-2},{0,0,-2},{0,0,2}},{{-2,0,-2},{0,0,2},{-2,0,2}},
            {{1,0,-2},{2,0,-2},{2,0,2}},{{1,0,-2},{2,0,2},{1,0,2}}};
        GroundPolygon edge;edge.boundaryOnly=true;edge.points={{0,-2},{0,2}};s.ground.push_back(edge);
        a={};a.position[0]=-.5f;stepActor(s,a,{1,0},false,1,true);
        if(a.position[0]>0||!floorHeight(s.floor,{a.position[0],a.position[2]}))
            throw std::runtime_error("Source movement entered a real floor hole");
    }

    // Synthetic geometry isolates reentry policy; no replacement geometry is used by the game.
    Scene scene;scene.speed=1;scene.radius=0.25f;scene.cameras.resize(1);
    scene.cameras[0].right={1,0};scene.cameras[0].forward={0,1};
    scene.floor.push_back({{0,0,0},{2,0,0},{0,0,2}});
    GroundPolygon boundary;boundary.boundaryOnly=true;boundary.points={{0,0},{2,0}};scene.ground.push_back(boundary);
    Actor actor;actor.position[0]=0.5f;actor.position[2]=-0.1f;actor.clearanceRadius=scene.radius;
    stepActor(scene,actor,{0,1},false,0.2f,true);
    if(actor.position[2]<0.09f||!floorHeight(scene.floor,{actor.position[0],actor.position[2]})||!clearGround(scene.ground,{actor.position[0],actor.position[2]},actor.clearanceRadius))
        throw std::runtime_error("Modern input could not reenter the floor from a native scripted pose");

    GroundPolygon solid;solid.points={{0.3f,0.2f},{0.8f,0.2f},{0.8f,0.5f},{0.3f,0.5f}};scene.ground.push_back(solid);
    actor={};actor.position[0]=0.5f;actor.position[2]=-0.3f;actor.clearanceRadius=0.1f;
    stepActor(scene,actor,{0,1},false,0.8f,true);
    if(actor.position[2]>0.2002f||!clearGround({solid},{actor.position[0],actor.position[2]},actor.clearanceRadius))
        throw std::runtime_error("Native pose reentry ignored an authored blocking volume");

    actor={};actor.position[0]=0.5f;actor.position[2]=0.5f;actor.clearanceRadius=0.1f;
    stepActor(scene,actor,{0,-1},false,1.0f);
    if(actor.position[2]<0.0998f||!floorHeight(scene.floor,{actor.position[0],actor.position[2]}))
        throw std::runtime_error("Legacy movement lost floor-boundary collision");
    scene.ground.clear();GroundPolygon npc;npc.points={{1,0.5f}};npc.circleRadius=0.2f;
    // The old adaptive .1 field must not shrink nominal .25 NPC separation.
    actor={};actor.position[0]=0.3f;actor.position[2]=0.5f;actor.clearanceRadius=0.1f;
    stepActor(scene,actor,{1,0},false,1.0f,true,{npc});
    if(actor.position[0]>0.5502f||actor.position[0]<0.549f||actor.clearanceRadius!=0||npc.circleRadius!=0.2f)
        throw std::runtime_error("Modern movement crossed an NPC or applied its radius twice");
    stepActor(scene,actor,{1,1},false,0.2f,true,{npc});
    GroundPolygon separatedNpc=npc;separatedNpc.circleRadius+=scene.radius;
    if(actor.position[2]<=0.5f||!clearGround({separatedNpc},{actor.position[0],actor.position[2]},0))
        throw std::runtime_error("Modern movement could not slide past an NPC");
    World world(root,nullptr,presentMovieHeadless,true);
    auto& body=world.actors->at({4,0});
    // Explicit collision-filter fixture, independent of story activation and combat.
    body.flags=0;const auto baseline=world.manualActorObstacles().size();
    body.flags=0x41;body.radius=40;body.nativeSlot=1;
    auto obstacles=world.manualActorObstacles();
    if(obstacles.size()!=baseline+1||std::none_of(obstacles.begin(),obstacles.end(),[&](const auto& polygon){return std::abs(polygon.circleRadius-0.4f)<1e-6f&&polygon.points[0].x==body.pose.x&&polygon.points[0].z==body.pose.z;}))
        throw std::runtime_error("Frozen physical NPC lost its collision radius");
    for(int flags:{0,9}){body.flags=flags;if(world.manualActorObstacles().size()!=baseline)throw std::runtime_error("Inactive NPC blocks modern input");}
    body.flags=1;body.nativeSlot=-1;
    if(world.manualActorObstacles().size()!=baseline)throw std::runtime_error("Unregistered NPC blocks modern input");
    std::cout<<"Source pose passed: idle XYZ/yaw, floor reentry, solid/boundary collision and modern NPC sweep/slide\n";
}
