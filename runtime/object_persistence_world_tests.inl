#pragma once
#undef NDEBUG
#include <cassert>
void testObjectGlitterWorld(World& world,int index) {
    world.step({},false,false,1.0f/30);assert(!world.scriptFault);
    const auto before=world.saveStateBytes();
    // Isolate a source-model fixture so unrelated emitters cannot hide an RNG wiring error.
    for(auto& [slot,object]:world.nativeProperties.objects){(void)slot;object.flags=int16_t(uint16_t(object.flags)&~1u);}
    auto& object=world.nativeProperties.objects.at(index);object.flags=0x0b;
    unsigned camera=0;bool visibleMarker=false;
    for(const auto& marker:world.scene.scripting.markers) {
        const auto floor=floorHeight(world.scene.floor,{marker.x,marker.z});if(!floor)continue;
        for(camera=0;camera<world.scene.cameras.size();++camera)if(world.scene.queries.visible(4,camera,{marker.x,*floor,marker.z})) {
            object.position={marker.x,0,marker.z};visibleMarker=true;break;
        }
        if(visibleMarker)break;
    }
    assert(visibleMarker);world.controlEpisodeCamera({2,int(camera),0,0,0});
    world.sampleEpisodeFloors();world.initializeVfx();
    const auto p=object.position;const float y=p.y+object.cachedFloorHeight;
    assert(world.scene.queries.visible(4,world.sourceQueryCamera(),{p.x,y,p.z}));
    const auto empty=world.vfx->snapshot();
    auto unchanged=[&] {
        const auto seed=world.nativeProperties.randomSeed,phase=world.nativeProperties.randomIndex;
        world.sampleEpisodeEffects();
        assert(world.nativeProperties.randomSeed==seed&&world.nativeProperties.randomIndex==phase&&world.vfx->snapshot()==empty);
    };
    object.flags=3;unchanged();object.flags=10;unchanged();object.flags=11;
    world.visibilityReady=false;unchanged();world.visibilityReady=true;
    const auto visibleCamera=world.visibilityCamera;unsigned hiddenCamera=0;
    while(hiddenCamera<world.scene.cameras.size()&&world.scene.queries.visible(4,hiddenCamera,{p.x,y,p.z}))++hiddenCamera;
    assert(hiddenCamera<world.scene.cameras.size());world.visibilityCamera=hiddenCamera;unchanged();world.visibilityCamera=visibleCamera;
    auto gate=[&](bool emit) {
        for(unsigned n=0;n<10000;++n) {
            auto probe=world.nativeProperties;
            if((probe.randomBound(256)<=6)==emit)return;
            world.nativeProperties.randomBound(256);
        }
        throw std::runtime_error("Cannot reach original object glitter RNG fixture");
    };
    gate(false);auto missed=world.nativeProperties;assert(missed.randomBound(256)>6);
    world.sampleEpisodeEffects();
    assert(world.vfx->snapshot()==empty&&world.nativeProperties.randomSeed==missed.randomSeed&&
           world.nativeProperties.randomIndex==missed.randomIndex&&world.nativeProperties.randomTable==missed.randomTable);
    gate(true);auto expected=world.nativeProperties;assert(expected.randomBound(256)<=6);
    edvfx::Host host;host.randomBound=[&](int bound){return expected.randomBound(bound);};
    host.cachedBound=[&](int bound){return expected.randomRange(0,bound);};
    edvfx::SourceVfx emitter(world.root,std::move(host));assert(emitter.spawn(23,{{p.x,y,p.z}},0));
    world.sampleEpisodeEffects();
    assert(world.vfx->snapshot()==emitter.snapshot()&&world.nativeProperties.randomSeed==expected.randomSeed&&
           world.nativeProperties.randomIndex==expected.randomIndex&&world.nativeProperties.randomTable==expected.randomTable);
    assert(world.vfx->particles().at(23).front().position==edvfx::Vector({{p.x,y,p.z}}));
    object.fade.info(127,0,0,1,0);world.sampleEpisodeEffects();
    const auto objects=world.drawObjects();assert(objects.size()==1&&objects.front().mode==1);
    assert((objects.front().tint&0x00ff0000u)&&!(objects.front().tint&0x0000ffffu));
    auto& actorFade=world.nativeProperties.actorFades[{0,0}];actorFade.info(0,0,0,4,0);actorFade.sample(world.sceneEffectTint());
    assert(world.drawActorTints().at({0,0}).mode==4&&world.drawActorTints().at({0,0}).color==0xff000000u);
    const auto active=world.saveStateBytes();world.restoreStateBytes(active);
    assert(world.saveStateBytes()==active&&stageSavedWorld(world,active)->saveStateBytes()==active);
    assert(world.drawObjects().front().mode==1&&world.drawActorTints().at({0,0}).mode==4);
    world.restoreStateBytes(before);
    std::cout<<"DC object glitter: visible bit8 only, simple RNG gate, original kind23/world floor, active direct+cold save passed\n";
}
void testObjectPersistenceWorld(const fs::path& root) {
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize source object persistence proof");
    struct Quit {~Quit(){SDL_Quit();}} quit;
    World world(root,nullptr,presentMovieHeadless,true);auto& vm=world.game.vm;const auto& package=world.game.package;
    const auto I=[](int value){return jpc::Value::integer(value);};const auto R=[](jpc::Ref value){return jpc::Value::reference(value);};
    const auto spawn=[&](const char* name,const char* signature,std::vector<jpc::Value> args){return vm.invoke(package.methodIndex("SpawnCommander",name,signature),0,std::move(args));};
    // Explicit persistent resource fixture, configured only through the original SpawnCommander API.
    // Ordinary resource0 defaults40000(reap on leave); clear it to exercise the source's live-object save branch.
    assert(spawn("spawnSetSpawnObjectType","(II)I",{I(0),I(119)}).integer()>=0);
    spawn("spawnSubResourceFlag","(II)V",{I(0),I(0x40000)});
    assert(spawn("spawnByMarker","(IIZI)Z",{I(0),I(33),I(0),I(119)}).integer());
    const int index=vm.arrayValues(vm.staticField(785).reference()).at(0).integer();assert(index>=0);
    const auto object=vm.arrayValues(vm.staticField(700).reference()).at(size_t(index)).reference();assert(vm.field(object,698).integer());
    const auto orientation=package.methodIndex("ed/base/EDObj","setOrientation","(III)V");
    const auto position=package.methodIndex("ed/base/EDObj","setPosition","(III)V");
    const auto rotY=package.methodIndex("ed/base/EDObj","getRotY","()I");
    for(const auto& edge:std::vector<std::pair<int,int>>{{5,4},{17,16},{-5,-4},{45,45},{90,90},{180,180},{359,358},{-359,-358},{360,360},{720,720}}){
        vm.invoke(orientation,object,{I(0),I(edge.first),I(0)});
        assert(world.nativeProperties.objects.at(index).orientation[1]==edge.first); // Retain requested degrees; reconstruct float radians in the getter.
        assert(vm.invoke(rotY,object).integer()==edge.second);
    }
    vm.invoke(orientation,object,{I(0),I(17),I(0)});
    const auto before=world.nativeProperties.objects.at(index);
    const std::array<int,3> expected{int(double(before.position.x)*100.f),int(double(before.position.y)*100.f),int(double(before.position.z)*100.f)};
    const auto stream=vm.makeArray('B',std::vector<jpc::Value>(512,I(0)));
    const auto init=package.methodIndex("ed/base/EDBase","initBitStream","([B)V");
    vm.invoke(init,0,{R(stream)});vm.invoke(package.methodIndex("Obj","save","()V"),object);const int used=vm.staticField(16).integer();assert(used==12);
    vm.invoke(position,object,{I(-80),I(80),I(1)});
    assert(vm.invoke(package.methodIndex("ed/base/EDObj","getPositionX","()I"),object).integer()==-79);
    assert(vm.invoke(package.methodIndex("ed/base/EDObj","getPositionY","()I"),object).integer()==79);
    assert(vm.invoke(package.methodIndex("ed/base/EDObj","getPositionZ","()I"),object).integer()==0);
    assert(world.nativeProperties.objects.at(index).flags==before.flags&&world.nativeProperties.objects.at(index).cachedFloorHeight==before.cachedFloorHeight);
    vm.invoke(orientation,object,{I(0),I(-90),I(0)});
    vm.invoke(init,0,{R(stream)});vm.invoke(package.methodIndex("Obj","restore","()V"),object);
    const auto& after=world.nativeProperties.objects.at(index);
    assert(after.position.x==float(double(expected[0])*.01f)&&after.position.y==float(double(expected[1])*.01f)&&after.position.z==float(double(expected[2])*.01f));
    assert(after.orientation[1]==16&&after.flags==before.flags&&after.cachedFloorHeight==before.cachedFloorHeight);
    const std::array<int,3> reload{int(double(after.position.x)*100.f),int(double(after.position.y)*100.f),int(double(after.position.z)*100.f)};
    world.beginNativeStage();world.endEpisodeScene();world.enterSourceScene(world.scene,world.actor,world.gates,false);world.commitNativeStage();
    const int restored=vm.arrayValues(vm.staticField(785).reference()).at(0).integer();assert(restored>=0);
    const auto& value=world.nativeProperties.objects.at(restored);
    assert(value.position.x==float(double(reload[0])*.01f)&&value.position.y==float(double(reload[1])*.01f)&&value.position.z==float(double(reload[2])*.01f));
    assert(value.orientation[1]==16);
    testObjectGlitterWorld(world,restored);
    std::cout<<"Actual SpawnCommander119 -> Obj.save840/restore836(12bytes,yaw17->16) -> full Main.endScene/SpawnSave980/SpawnRestore962, original local cm quantization and floor flags: passed\n";
}
