#pragma once
#include <cstdint>

struct SceneViewport { unsigned x, y, width, height; };
inline SceneViewport sceneViewport(unsigned width, unsigned height) {
    if (!width || !height) return {};
    unsigned sceneWidth = width, sceneHeight = height;
    if (uint64_t(width) * 3 > uint64_t(height) * 4) sceneWidth = height * 4 / 3;
    else sceneHeight = width * 3 / 4;
    return {(width - sceneWidth) / 2, (height - sceneHeight) / 2, sceneWidth, sceneHeight};
}
