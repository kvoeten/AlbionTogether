#pragma once

#include <cmath>

namespace fable::game
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        [[nodiscard]] float HorizontalDistanceTo(const Vector3& other) const noexcept
        {
            const float deltaX = other.x - x;
            const float deltaY = other.y - y;
            return std::sqrt(deltaX * deltaX + deltaY * deltaY);
        }
    };
}
