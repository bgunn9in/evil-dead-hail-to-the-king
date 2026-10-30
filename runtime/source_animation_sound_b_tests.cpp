#include "actor_startup_fixture.h"
#include "audio_bank.h"

int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected original LevelB asset root");const std::filesystem::path root=argv[1];
    struct Case {int prototype,type,action;const char* scene;};
    for(const auto c:std::array<Case,10>{{{19,19,3,"bc01"},{23,23,12,"bl06"},{26,26,3,"bt11"},{27,27,-1,"ba04"},
            {28,28,3,"bg03"},{29,29,11,"bg12"},{31,31,-1,"ba11"},{36,31,-1,"ba11"},{37,31,-1,"ba11"},{33,33,-1,"bc02"}}}){
        AudioBank bank(root);unsigned received=0;StartupCase* source=nullptr;std::vector<std::pair<uint32_t,int>> records;
        StartupCase fixture(root/"scripts/levelB.jpc",root,true,[&](auto& host){
            host.animationSound=[&](const auto& body,const auto& raw){if(body.key.type!=c.type)return;
                const auto sound=edanimevent::decodeSound(raw);assert(sound.enabled&&edanimsound::verifiedActor(body.key.type));
                bool actualFrame=false;for(const auto& playback:body.actions.playbacks)if(playback.active&&playback.mode!=4)actualFrame|=playback.frame==sound.frame;
                assert(actualFrame);const int handle=bank.resolve(c.scene,sound.resource,true);assert(handle>=0);
                const int voice=bank.playAtFrequency(handle,100,sound.frequencyHz());assert(voice>=0&&!bank.done(voice));
                const auto state=bank.snapshot();const auto& played=state.voices.back();
                assert(played.token==voice&&played.volume==100&&played.frequencyHz==sound.frequencyHz()&&!played.loop);
                records.emplace_back(sound.resource,sound.frequencyHz());++received;
            };
        });source=&fixture;auto& game=source->game;auto& actors=*source->actors;auto& vm=game.vm;const auto& p=game.package;
        game.enterScene(c.scene);vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(c.prototype);assert(npc);
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& body=actors.at(key);
            body.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(body.think))});vm.hold(body.think);}
        for(int mask:{8,32,64})vm.invoke(p.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(p.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});
        if(c.action>=0)assert(actors.consoleAction({c.type,0},c.action));
        const auto oldVm=vm.snapshot(),oldActors=actors.snapshot();const auto oldAudio=bank.snapshot();
        auto run=[&](){unsigned tick=0;while(!received&&tick<100){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);}assert(received>0);return tick;};
        const auto tick=run();const auto expected=records;const auto audio=bank.snapshot();
        vm.restore(oldVm);actors.restore(oldActors);bank.restore(oldAudio);records.clear();received=0;
        assert(run()==tick&&records==expected&&bank.snapshot().nextVoice==audio.nextVoice);
        std::cout<<"LevelB prototype"<<c.prototype<<" native"<<c.type<<": real FSM/raw20 -> exact scene/global_b PCM + original pitch/volume/token replay: passed\n";
    }
    std::cout<<"Source Merchant24/Captain38/PumpKeeper39 have zero reachable raw20 records (checked separately against original archives). Hardware audio is not opened in this CPU proof.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
