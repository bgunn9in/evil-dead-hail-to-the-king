#pragma once
// Source hardware/configuration fixtures; no HP, actor AI or gameplay rule changes.
void testSourceRumbleWorld(const fs::path& root) {
    auto require=[](bool okay,const char* text){if(!okay)throw std::runtime_error(std::string("Source rumble: ")+text);};
    const auto stop=edrumble::modernMotor({0,0,0});require(stop.amplitude==0,"source stop did not stop modern motors");
    const auto full=edrumble::modernMotor({0,0,150});require(full.power==7&&full.frequency==59&&full.amplitude==65535,"native default strength7/frequency59 differs");
    const auto low=edrumble::modernMotor({0,32,1}),negative=edrumble::modernMotor({0,-32,1});
    require(low.power==2&&low.frequency==8&&low.amplitude==18724&&negative.power==-2&&negative.frequency==59&&negative.amplitude==18724,
        "native signed strength/SDK +/-1 promotion or bounded frequency differs");
    require(edrumble::modernMotor({0,31,1}).amplitude==0&&edrumble::modernMotor({0,255,1}).amplitude==65535,
        "native zero/full encoded strength differs");
    for(const auto& directory:std::array<fs::path,2>{{root,root/"levelB"}}) {
        std::vector<edrumble::Command> output;unsigned capabilities=0;
        edrumble::Host host;host.available=[&](int port){++capabilities;return port==0;};host.rumble=[&](edrumble::Command command){output.push_back(command);};
        World world(directory,nullptr,presentMovieHeadless,true,nullptr,false,host);auto& vm=world.game.vm;
        const int start=world.game.package.methodIndex("RumbleCommander","startRumble","(IIIIII)V");const auto& method=world.game.package.methods.at(size_t(start));
        const auto* code=world.game.package.data.data()+56+method.offset;
        require(code[0]==0xb2&&code[7]==0xb3,"original rumble source field layout changed");
        const auto enabled=world.game.package.constant(method.owner,(unsigned(code[1])<<8)|code[2]).bits;
        const auto countdown=world.game.package.constant(method.owner,(unsigned(code[8])<<8)|code[9]).bits;
        require(vm.staticField(enabled).integer()==1,"capable host was not available before original options initialization");
        const auto feedbackBefore=world.saveStateBytes();
        require(world.chainsawFeedback().enabled,"modern feedback did not read the original A/B rumble option");
        require(world.saveStateBytes()==feedbackBefore,"reading modern feedback mutated source state");
        const auto ability=world.game.package.methodIndex("ed/base/External","checkRumbleAbility","(I)I");
        require(vm.invoke(ability,0,{jpc::Value::integer(0)}).integer()==1&&vm.invoke(ability,0,{jpc::Value::integer(1)}).integer()==0,
            "original ability query did not use active host port capability");output.clear();
        const auto fire=[&]{vm.invoke(world.game.package.methodIndex("RumbleCommander","startRumble","(I)V"),0,{jpc::Value::integer(8)});};
        const auto before=world.game.checkpoint();const auto nativeBefore=world.checkpointNative();world.beginNativeStage();fire();
        require(output.empty()&&vm.staticField(countdown).integer()==4,"source gun profile was emitted physically before transaction commit");
        world.game.rollback(before);world.rollbackNative(nativeBefore);require(output.empty()&&world.preparedRumbles.empty(),"rejected source stage emitted rumble");
        world.beginNativeStage();fire();require(output.empty(),"prepared source rumble bypassed native transaction");world.commitNativeStage();
        require(output.size()==1&&output[0].port==0&&output[0].a==0&&output[0].b==150&&vm.staticField(countdown).integer()==4,
            "original gun profile or committed raw command differs");
        const auto tick=world.game.package.methodIndex("RumbleCommander","rumbleProcessFrame","()V");
        for(int remaining=3;remaining>=0;--remaining){vm.invoke(tick);require(vm.staticField(countdown).integer()==remaining&&output.size()==1,"original source countdown/stop timing differs");}
        vm.invoke(tick);require(vm.staticField(countdown).integer()==-1&&output.size()==2&&output[1].a==0&&output[1].b==0,"source expiry did not emit original stop command");
        // Original menu setting fixture: disabled rumble must not call the physical host or reset duration.
        vm.statics[enabled]=jpc::Value::integer(0);fire();require(output.size()==2&&vm.staticField(countdown).integer()==-1,"source disabled option did not gate physical output");
        require(!world.chainsawFeedback().enabled,"modern feedback ignored original disabled rumble option");
        const auto saved=world.saveStateBytes();output.clear();
        {auto restored=stageSavedWorld(world,saved);require(output.empty(),"saved-world validation emitted physical feedback");
            restored->rumbleHost.send({0,0,150});require(output.size()==1,"restored world lost its feedback endpoint");}
        output.clear();auto truncated=saved;truncated.pop_back();bool rejected=false;
        try{auto invalid=stageSavedWorld(world,truncated);}catch(const std::exception&){rejected=true;}
        require(rejected&&output.empty(),"rejected saved-world staging emitted physical feedback");
        vm.invoke(world.game.package.methodIndex("ed/base/External","sndMgrSetMasterVolume","(I)V"),0,{jpc::Value::integer(37)});
        vm.invoke(world.game.package.methodIndex("ed/base/External","sndMgrSetStereo","(I)V"),0,{jpc::Value::integer(0)});
        world.beginNativeStage();world.resetOriginalOptions();
        require(world.sourceGui.masterVolume==100&&world.sourceGui.stereo==1&&vm.staticField(enabled).integer()==1,
            "game settings reset did not restore original audio/rumble defaults");
        require(output.empty(),"game settings reset emitted feedback before commit");world.commitNativeStage();
        std::cout<<"Source rumble "<<directory<<": ctor capability calls"<<capabilities<<", original gun4frames/stop, enabled gate, native commit/discard passed\n";
    }
    World none(root,nullptr,presentMovieHeadless,true);
    require(none.game.vm.invoke(none.game.package.methodIndex("ed/base/External","checkRumbleAbility","(I)I"),0,{jpc::Value::integer(0)}).integer()==0,"default CPU host advertised physical rumble");
    none.resetOriginalOptions();require(none.sourceGui.masterVolume==100&&none.sourceGui.stereo==1,"no-motor host game settings reset failed");
    std::cout<<"Source rumble passed: native strength encoding, original A/B duration/options and transaction-safe physical callback\n";
}
