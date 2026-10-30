#pragma once
// Bounded test input. Every object receipt and health effect executes original JPC.
static bool collectNearbySourceHealth(World& world,float radius=3.f){auto& vm=world.game.vm;
    if(world.game.mode!=0||world.guard||vm.field(world.game.ash,13).integer()>65)return false;
    const Vec2 start{world.actor.position[0],world.actor.position[2]};std::optional<Vec2> target;int identity=0;float best=radius;
    const int flagsMethod=world.game.package.methodIndex("ObjDB","getObjectFlags","(I)I"),healthMethod=world.game.package.methodIndex("ObjDB","getObjectValueHealth","(I)I");
    for(const auto&[slot,o]:world.nativeProperties.objects)if(o.flags&1){const int id=o.setup[0];const int flags=vm.invoke(flagsMethod,0,{jpc::Value::integer(id)}).integer();
        if(!(flags&4)||vm.invoke(healthMethod,0,{jpc::Value::integer(id)}).integer()<=0)continue;const float d=length(start-Vec2{o.position.x,o.position.z});if(d<best){best=d;identity=id;target=Vec2{o.position.x,o.position.z};}}
    if(!target)return false;
    for(const auto&[key,a]:world.actors->bodies())if(key.type!=0&&(a.flags&1)&&!(a.flags&8)&&vm.field(a.script,13).integer()>0&&length(start-Vec2{a.pose.x,a.pose.z})<3.f)return false;
    auto count=[&](int id){int n=0;for(const auto&i:world.game.items(99))if(i.itemID==id)n+=i.quantity;return n;};
    auto toward=[&](Vec2 point,float amount){const auto&c=world.scene.cameras.at(world.actor.camera);const auto&l=world.actor.movement;const Vec2 right=l.held?l.right:c.right,forward=l.held?l.forward:c.forward,d=point-Vec2{world.actor.position[0],world.actor.position[2]};const float det=right.x*forward.z-right.z*forward.x;if(std::abs(det)<.001f)throw std::runtime_error("Pickup camera basis is singular");const Vec2 stick{(d.x*forward.z-d.z*forward.x)/det,(right.x*d.z-right.z*d.x)/det};return length(stick)>.000001f?stick*(amount/length(stick)):Vec2{};};
    const auto area=world.scene.id;auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f){ControlFrame in;in.movement=stick;world.playerControls(in);world.step(stick,true,use,dt);if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);if(world.scene.id!=area||vm.field(world.game.ash,13).integer()<=0)throw std::runtime_error("Actual health pickup input left area or died");};
    auto ready=[&]{for(unsigned n=0;n<240;++n){if(world.inventoryView.open)world.closeInventory();const auto&a=world.actors->at({0,0});if(world.game.mode==0&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&a.actions.possible(116))return;tick();}throw std::runtime_error("Source pickup action did not become ready");};
    ready();for(unsigned n=0;n<90;++n){const Vec2 here{world.actor.position[0],world.actor.position[2]},away=here-*target;const float d=length(away);if(d<1.05f)break;const Vec2 goal=*target+away*(.9f/d);
        if(!floorHeight(world.scene.floor,goal)||!clearGround(world.scene.ground,goal,0)||length(moveGround(world.scene.ground,here,goal-here,0)-goal)>ContactSkin*4)return false;
        tick(toward(goal,std::min(1.f,length(goal-here)/(world.scene.speed*2*.033f))));}
    const int before=count(identity);bool received=false;
    std::cout<<"SOURCE_HEALTH_APPROACH id="<<identity<<" range="<<length(*target-Vec2{world.actor.position[0],world.actor.position[2]})<<'\n';
    for(unsigned attempt=0;attempt<6&&!received;++attempt){ready();tick(toward(*target,1),false,0);tick({},true,0);
        for(unsigned n=0;n<180&&count(identity)==before&&!world.inventoryView.open;++n)tick();
        received=count(identity)>before;std::cout<<"GENUINE_HEALTH_RECEIPT attempt="<<attempt<<" desired="<<identity<<" quantity="<<before<<"->"<<count(identity)<<" sourceTick="<<world.sourceTick<<'\n';
        if(world.inventoryView.open)world.closeInventory();}
    if(!received)throw std::runtime_error("Original pickup did not receive the real nearby health object");
    while(useSourceHealingItem(world)){}return true;
}
