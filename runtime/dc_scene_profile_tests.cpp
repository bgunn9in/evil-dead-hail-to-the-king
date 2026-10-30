#include "dc_scene_profile.h"
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}assert(rejected);}
int main(int argc,char** argv) {
    assert(argc==1||argc==2);
    for(unsigned start:{0u,1u,14u,15u,29u}) {
        assert(eddcprofile::foregroundFrame("bl06",5,start)==start);
        for(unsigned camera:{0u,4u,6u,17u})assert(eddcprofile::foregroundFrame("bl06",camera,start)==0);
        for(const auto* scene:{"ak01","ak09","bm11","bl05","bl07"})
            for(unsigned camera:{0u,2u,5u,9u,10u})assert(eddcprofile::foregroundFrame(scene,camera,start)==0);
    }
    struct Profile {const char* text;unsigned width,height;bool native;};
    for(const auto expected:{Profile{"EDPROFILEBG1 320 240 .875",320,240,true},
                             Profile{"EDPROFILEBG1 640 480 .875",640,480,true},
                             Profile{"EDPROFILEBG1 640 384 1",640,384,false}}) {
        std::istringstream input(expected.text);const auto p=eddcprofile::background(input);
        assert(p.width==expected.width&&p.height==expected.height&&p.native==expected.native);
    }
    for(const auto text:{"","EDPROFILEBG1 320 240 1","EDPROFILEBG1 640 480 1","EDPROFILEBG1 320 240 .875 extra","EDPROFILEBG1 -1 240 .875",
                         "EDPROFILEBG1 640 240 .875","EDPROFILEBG1 320 480 .875","EDPROFILEBG1 640 384 .875","EDPROFILEBG1 1280 960 .875"})
        rejects([&]{std::istringstream input(text);eddcprofile::background(input);});
    std::istringstream actual("EDDCMASKDRAW1 2\n0 0 410 390 4.380799770355225 0.2282688170671463 .875 .640625 .8125\n190 195 250 210 4.365900039672852 0.22904784977436066 .875 .390625 .4375");const auto layers=eddcprofile::masks(actual);
    assert(layers.size()==2&&layers[0].width==410&&layers[1].inverseZ==.22904784977436066f);
    for(const auto text:{"EDDCMASKDRAW1 0","EDDCMASKDRAW1 1 0 0 650 480 1 1 .875 1.015625 1"}) {
        std::istringstream input(text);eddcprofile::masks(input);
    }
    for(const auto text:{"","EDDCMASKDRAW1 33","EDDCMASKDRAW1 1 0","EDDCMASKDRAW1 0 extra",
        "EDDCMASKDRAW1 1 0 0 410 390 0 1 .875 .640625 .8125",
        "EDDCMASKDRAW1 1 0 0 410 390 1 .5 .875 .640625 .8125",
        "EDDCMASKDRAW1 1 0 0 410 390 1 1 .25 .640625 .8125",
        "EDDCMASKDRAW1 1 0 0 411 390 1 1 .875 .640625 .8125",
        "EDDCMASKDRAW1 1 0 0 410 390 1 1 .875 1 .8125",
        "EDDCMASKDRAW1 1 0 0 410 390 nan 1 .875 .640625 .8125"})
        rejects([&]{std::istringstream input(text);eddcprofile::masks(input);});
    const std::array<float,16> vp={1.1998664144620792f,1.2952446043651134f,.4896127717083109f,.4895736026865743f,
        0,1.5716837014572829f,-.7462102946043409f,-.7461505977807725f,
        -1.3019267908691714f,1.1937080565439366f,.4512311483138729f,.4511950498220078f,
        1.4131089956831164f,-7.4585055246437015f,2.632371945042648f,2.682161355287045f};
    const auto mapping=eddcprofile::depthMapping(vp);
    assert(std::abs(mapping.a-1.00007997425)<1e-10&&std::abs(mapping.b+.0500039325394)<1e-10);
    assert(mapping.map(layers[0].inverseZ)==.9886656403541565f);
    assert(mapping.map(1.0f/2.5f)<mapping.map(layers[1].inverseZ));
    assert(mapping.map(1.0f/7.0f)>mapping.map(layers[0].inverseZ));
    rejects([&]{mapping.map(100);});
    for(unsigned mode:{0u,1u,2u})rejects([&]{auto bad=vp;if(mode==0)bad[3]=0;if(mode==1)bad[2]=0;if(mode==2)bad[0]=std::numeric_limits<float>::infinity();eddcprofile::depthMapping(bad);});
    if(argc==2) {
        unsigned backgrounds=0,layerCount=0,overscan=0;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[1]))if(entry.path().extension()==".masks") {
            std::ifstream input(entry.path());const auto source=eddcprofile::masks(input);
            ++backgrounds;layerCount+=unsigned(source.size());
            for(const auto& layer:source)overscan+=layer.width==650;
        }
        assert(backgrounds==4330&&layerCount==570&&overscan==38);
    }
}
