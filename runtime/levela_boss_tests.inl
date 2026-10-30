static void levelaBossTests(ScriptGame& game,edactors::GameplayActors& actors,SourceActorRoutes& routes,int& milliseconds,
        const std::vector<uint8_t>& initialVm,const std::vector<uint8_t>& initialActors,const std::vector<uint8_t>& initialRoutes) {
    auto& vm=game.vm;const auto& p=game.package;
    struct Case {int type;const char* name;const char* scene;const char* owner;const char* sceneOwner;int states,bones,vertices,base,damage;};
    for(const auto c:std::array<Case,3>{{{14,"Matriarch","ah06","MatriarchBoss","SceneAH06",19,32,2148,45,12},
            {17,"DeadAsh","ag03","DeadAsh","SceneAG03",31,25,2112,65,15},
            {18,"Annie","au04","AnnieZombie","SceneAU04",22,57,2838,68,10}}}) {
        const auto attackMethod=p.methodIndex(c.owner,"actionAttack","()V");
        vm.restore(initialVm);actors.restore(initialActors);routes.restore(initialRoutes);milliseconds=0;
        game.enterScene(c.scene);vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));
        const auto npc=actors.createType(c.type);const edactors::Key key{c.type,0};assert(npc&&!actors.createType(c.type));
        assert(vm.objectClass(npc)==p.classIndex(c.owner)&&vm.objectClass(vm.field(npc,4).reference())==p.classIndex("ed/base/EDBrain"));
        assert(vm.field(npc,13).integer()==100&&vm.field(npc,1212).integer()==100);
        assert(vm.arrayValues(vm.staticField(54).reference())[size_t(c.type)].integer()==c.base);
        const auto initialParts=actors.at(key).parts;for(bool visible:initialParts)assert(visible);
        vm.invoke(p.methodIndex(c.sceneOwner,"spawnMonster1","(LGenericNpc;III)V"),game.scene,{jpc::Value::reference(npc),I(0),I(0),I(0)});
        const auto& body=actors.at(key);assert(int(body.actions.states.size())==c.states&&int(body.localPose.rotationsDegrees.size())==c.bones&&int(body.vertices.size())==c.vertices);
        if(c.type==17)assert(body.parts==std::vector<bool>({true,true}));
        if(c.type==18)assert(body.parts==std::vector<bool>({true,true,true,false}));
        routes.actorCreated(key);
        const auto route=vm.invoke(p.methodIndex("ed/base/External","routeToActor","(IIII)I"),0,{I(4),I(0),I(c.type),I(0)}).integer();assert(route>=0);
        routes.restore(routes.snapshot());vm.invoke(p.methodIndex("ed/base/External","routeDestroy","(I)V"),0,{I(route)});
        // Park this original queued ActorDB thinker before its first turn;
        // event callbacks, native actions and source frame waits still execute.
        auto& native=actors.at(key);native.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(c.type),I(0)});
        vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(c.type),I(0),I(int32_t(native.think))});vm.hold(native.think);
        bool park=true;uint64_t tick=0;
        auto step=[&](bool press=false) {
            const auto thinker=actors.at(key).think;if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Complete){
                if(park&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
                if(!park&&vm.status(thinker)==jpc::Interpreter::ThreadStatus::Held)vm.awake(thinker);}
            if(press)actors.playerAction(17);milliseconds=int(++tick*1000/30);vm.statics[398]=I(milliseconds);actors.update(tick);
        };
        auto place=[&](float x,float z,float yaw){auto& ash=actors.at({0,0});ash.pose={x,ash.cachedFloorHeight,z,yaw};ash.localY=0;
            auto& enemy=actors.at(key);enemy.pose={0,enemy.cachedFloorHeight,0,c.type==14?0.0f:edactormath::Pi};enemy.localY=0;};
        place(c.type==14?1.f:0.f,c.type==14?0.f:1.f,c.type==14?-edactormath::HalfPi:edactormath::Pi);
        if(c.type==14) {
            bool contact=false;for(unsigned n=0;n<140&&!contact;++n){step(true);contact=vm.status(actors.at(key).think)!=jpc::Interpreter::ThreadStatus::Held;}
            assert(contact&&vm.field(npc,13).integer()==100);
            const auto stop=vm.start(p.methodIndex("GenericNpc","actionStopWalk","()V"),npc);
            for(unsigned n=0;n<100&&vm.status(stop)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
            assert(vm.status(stop)==jpc::Interpreter::ThreadStatus::Complete);vm.forget(stop);
            assert(actors.at(key).actions.layers[0].state==0);place(1,0,-edactormath::HalfPi);
        }
        const int ashBefore=vm.field(game.ash,13).integer();const auto attack=vm.start(attackMethod,npc);
        const auto attackVm=vm.snapshot(),attackActors=actors.snapshot(),attackRoutes=routes.snapshot();const auto attackTick=tick;
        for(unsigned n=0;n<240&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(vm.status(attack)==jpc::Interpreter::ThreadStatus::Complete&&vm.field(game.ash,13).integer()==ashBefore-c.damage);
        const auto hitTick=tick;vm.restore(attackVm);actors.restore(attackActors);routes.restore(attackRoutes);tick=attackTick;
        for(unsigned n=0;n<240&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(tick==hitTick&&vm.field(game.ash,13).integer()==ashBefore-c.damage);vm.forget(attack);
        for(unsigned n=0;n<90&&vm.field(game.ash,11).integer();++n)step();
        bool sawHurt=false,phase=false,sawDeathAnimation=false;jpc::Interpreter::ThreadID window=0;
        const auto aliveVm=vm.snapshot(),aliveActors=actors.snapshot(),aliveRoutes=routes.snapshot();const auto aliveTick=tick;
        for(unsigned n=0;n<5500&&vm.field(npc,13).integer()>0;++n) {
            if(c.type==14) {
                // Front-facing action1732 takes its original non-damaging
                // attack-and-wait branch; its native attack states accept44/45.
                actors.at(key).pose.yaw=edactormath::HalfPi;
                if(window&&vm.status(window)==jpc::Interpreter::ThreadStatus::Complete){vm.forget(window);window=0;}
                if(!window)window=vm.start(attackMethod,npc);
            }
            step(true);sawHurt|=vm.field(npc,13).integer()<100;
            if(c.type==18&&!phase&&vm.field(npc,13).integer()<50) {
                park=false;bool transforming=false;
                for(unsigned wait=0;wait<250&&!vm.field(npc,1392).integer();++wait){transforming|=actors.at(key).actions.layers[0].state==1;step();}
                assert(vm.field(npc,1392).integer()==1&&vm.field(npc,13).integer()==100&&transforming);
                phase=true;park=true;
                const auto phaseVm=vm.snapshot(),phaseActors=actors.snapshot(),phaseRoutes=routes.snapshot();
                vm.restore(phaseVm);actors.restore(phaseActors);routes.restore(phaseRoutes);
                for(unsigned wait=0;wait<120;++wait)step();
                const int before=vm.field(game.ash,13).integer();const auto phaseAttack=vm.start(attackMethod,npc);
                for(unsigned wait=0;wait<200&&vm.status(phaseAttack)!=jpc::Interpreter::ThreadStatus::Complete;++wait)step();
                assert(vm.status(phaseAttack)==jpc::Interpreter::ThreadStatus::Complete&&vm.field(game.ash,13).integer()==before-15);vm.forget(phaseAttack);
                for(unsigned wait=0;wait<90&&vm.field(game.ash,11).integer();++wait)step();
            }
        }
        assert(sawHurt&&vm.field(npc,13).integer()<=0&&(c.type!=18||phase));
        if(window){for(unsigned n=0;n<240&&vm.status(window)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
            assert(vm.status(window)==jpc::Interpreter::ThreadStatus::Complete);vm.forget(window);}
        park=false;
        for(unsigned n=0;n<750&&vm.field(npc,game.hostField(HostFieldRole::GenericNpc30)).integer()!=1;++n) {
            for(const auto& layer:actors.at(key).actions.layers)if(layer.state>=0)
                sawDeathAnimation|=actors.at(key).actions.states[size_t(layer.state)].endEvent==24;
            step();
        }
        assert(vm.field(npc,game.hostField(HostFieldRole::GenericNpc30)).integer()==1&&!(actors.at(key).flags&1));
        if(c.type==18)assert(vm.staticField(608).integer()==1&&!sawDeathAnimation);else assert(sawDeathAnimation);
        const auto deadVm=vm.snapshot(),deadActors=actors.snapshot(),deadRoutes=routes.snapshot();
        vm.restore(deadVm);actors.restore(deadActors);routes.restore(deadRoutes);assert(actors.snapshot()==deadActors);
        // A living opaque save restores the exact original brain/HP/action wait.
        vm.restore(aliveVm);actors.restore(aliveActors);routes.restore(aliveRoutes);tick=aliveTick;
        assert(vm.field(npc,13).integer()==100&&actors.at(key).script==npc);
        vm.invoke(p.methodIndex("LevelA","endScene","()V"),game.level);const auto removed=actors.unloadScene();
        assert(removed==std::vector<edactors::Key>{key});routes.forgetActors(removed);actors.resetSceneRuntime();
        assert(actors.bodies().size()==9);std::cout<<c.name<<": source factory/parts/attack"<<c.damage<<"HP, source input17 damage/phase/death and VM/native/routes replay: passed\n";
    }
    vm.restore(initialVm);actors.restore(initialActors);routes.restore(initialRoutes);milliseconds=0;
}
