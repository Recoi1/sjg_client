#pragma once
#include "Game/Util/MathUtil.hpp"
namespace galaxy {
inline bool pointerToEdge(TVec2f* output, const TVec2f& position, const TVec2f& velocity,
                          float width, float height, float marginX, float marginY) {
    const auto inside = [&](const TVec2f& p) {
        return p.x >= marginX && p.y >= marginY && p.x <= width - marginX && p.y <= height - marginY;
    };
    if (!inside(position)) return false;
    TVec2f direction(velocity);
    MR::normalizeOrZero(&direction);
    if (direction.x != 0) {
        const float edge = direction.x > 0 ? width - marginX : marginX;
        const float time = (edge - position.x) / direction.x;
        const TVec2f candidate(position.x + direction.x * time, position.y + direction.y * time);
        if (inside(candidate)) { *output = candidate; return true; }
    }
    if (direction.y != 0) {
        const float edge = direction.y > 0 ? height - marginY : marginY;
        const float time = (edge - position.y) / direction.y;
        const TVec2f candidate(position.x + direction.x * time, position.y + direction.y * time);
        if (inside(candidate)) { *output = candidate; return true; }
    }
    return false;
}
}
