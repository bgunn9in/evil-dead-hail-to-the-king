#include "input.h"
#include "viewport.h"
#undef NDEBUG
#include <cassert>
#include <limits>

static bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
int main() {
    bool held=false;
    assert(!pressedEdge(false,held));
    assert(pressedEdge(true,held));
    assert(!pressedEdge(true,held));
    assert(!pressedEdge(false,held));
    assert(pressedEdge(true,held));
    assert(near(length(radialStick(0.1f, 0.1f)), 0));
    assert(near(length(radialStick(0.59f, 0)), 0.5f));
    assert(near(length(radialStick(1, 1)), 1));
    assert(near(length(radialStick(std::numeric_limits<float>::quiet_NaN(), 1)), 0));
    Movement movement;
    Vec2 result = movement.world({0, 1}, {1, 0}, {0, 1});
    assert(near(result.x, 0) && near(result.z, 1));
    result = movement.world({0, 0.5f}, {0, 1}, {-1, 0});
    assert(near(result.x, 0) && near(result.z, 0.5f));
    result = movement.world({1, 0}, {0, 1}, {-1, 0});
    assert(near(result.x, 0) && near(result.z, 1));
    movement.world({}, {1, 0}, {0, 1});
    result = movement.world({0, 1}, {0, 1}, {-1, 0});
    assert(near(result.x, -1) && near(result.z, 0));
    assert(near(frameSeconds(1), 0.05f));
    assert(near(frameSeconds(-1), 0));
    float at30 = 0, at60 = 0;
    for (int i = 0; i < 30; ++i) at30 += 2 * frameSeconds(1.0 / 30);
    for (int i = 0; i < 60; ++i) at60 += 2 * frameSeconds(1.0 / 60);
    assert(near(at30, at60));
    const SceneViewport standard = sceneViewport(1280, 960);
    assert(standard.x == 0 && standard.y == 0 && standard.width == 1280 && standard.height == 960);
    const SceneViewport ultrawide = sceneViewport(3440, 1440);
    assert(ultrawide.x == 760 && ultrawide.y == 0 && ultrawide.width == 1920 && ultrawide.height == 1440);
    const SceneViewport widescreen = sceneViewport(2560, 1440);
    assert(widescreen.x == 320 && widescreen.y == 0 && widescreen.width == 1920 && widescreen.height == 1440);
    const SceneViewport shortWindow = sceneViewport(1000, 300);
    assert(shortWindow.x == 300 && shortWindow.y == 0 && shortWindow.width == 400 && shortWindow.height == 300);
    const SceneViewport portrait = sceneViewport(600, 1000);
    assert(portrait.x == 0 && portrait.y == 275 && portrait.width == 600 && portrait.height == 450);
    assert(sceneViewport(0, 0).width == 0);
}
