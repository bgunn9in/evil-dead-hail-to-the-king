#include "script_scene.h"
#include "source_scene_queries.h"
#undef NDEBUG
#include <cassert>

int main() {
    ScriptVolume rectangle{6,0,{{0,100,0},{2,100,2}}};
    assert(rectangle.contains({0,-100,1}));assert(!rectangle.contains({-0.001f,0,1}));
    ScriptVolume triangle{6,1,{{0,1,0},{0,2,2},{2,3,0}}};
    assert(triangle.contains({0.5f,-100,0.5f}));assert(!triangle.contains({1,0,1}));
    ScriptVolume sphere{7,3,{{1,2,3},{0,2,0}}};
    assert(sphere.contains({1,4,3}));assert(!sphere.contains({1,4.001f,3}));
    sphere.shape=4;assert(sphere.contains({1,4,3}));assert(!sphere.contains({1,4.001f,3}));
    ScriptVolume pointSphere{7,4,{{1,2,3},{0,0,0}}};assert(pointSphere.contains({1,2,3}));assert(!pointSphere.contains({1,2.001f,3}));
    ScriptVolume roundedSphere{7,3,{{0,0,0},{.2f,.3f,.4f}}};assert(!roundedSphere.contains({.2f,.3f,.4f}));
    ScriptVolume box{6,5,{{0,0,0},{2,2,2}}};
    assert(box.contains({0,2,1}));assert(!box.contains({0,2.001f,1}));
    assert(!ScriptVolume{}.contains({0,0,0}));
    auto inverted=box;inverted.points[0].y=3;assert(!inverted.contains({1,2,1}));
    std::vector<uint8_t> data(88+72+12);
    auto integer=[&](size_t offset,uint32_t value){std::memcpy(data.data()+offset,&value,4);};
    auto point=[&](size_t offset,GroundPoint value){std::memcpy(data.data()+offset,&value,12);};
    integer(8,1);integer(12,88);integer(72,1);integer(76,160);
    data[88]=6;data[89]=1;point(92,{0,0,0});point(104,{0,0,2});point(116,{2,0,0});point(160,{0,0,3});
    ScriptScene scene(data);assert(scene.inside(0,{0.5f,0,0.5f}));
    data[89]=5;point(92,{0,0,0});point(104,{2,2,2});
    ScriptScene boxScene(data);assert(boxScene.volumes[0].points.size()==2&&boxScene.inside(0,{1,2,1}));
    SourceSceneQueries visibility;visibility.grid={0,0,16,16};box.kind=3;
    visibility.volumes.push_back({box,0xffffffff,15,2});
    for(unsigned category:{1u,2u,4u,8u}) {
        assert(visibility.visible(category,2,{1,2,1}));assert(!visibility.visible(category,2,{1,2.001f,1}));
        assert(!visibility.visible(category,1,{1,1,1}));
    }
    assert(scene.facing(0,40,{0,0},0.99f));assert(!scene.facing(0,40,{0,0},1.0f));
    data.resize(170);bool failed=false;
    try {ScriptScene invalid(data);}catch(const std::runtime_error&){failed=true;}assert(failed);
}
