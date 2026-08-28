#include "titan_animation.h"

float TitanAnimation_Approach(
    float current,
    float target,
    float factor
)
{
    if (factor <= 0.0f)
    {
        return current;
    }

    if (factor >= 1.0f)
    {
        return target;
    }

    return
        current +
        ((target - current) * factor);
}

float TitanAnimation_Clamp01(
    float value
)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }

    if (value > 1.0f)
    {
        return 1.0f;
    }

    return value;
}

float TitanAnimation_SmoothStep(
    float value
)
{
    float clamped;

    clamped = TitanAnimation_Clamp01(value);

    return
        clamped *
        clamped *
        (3.0f - (2.0f * clamped));
}

float TitanAnimation_EaseOutCubic(
    float value
)
{
    float clamped;
    float inverse;

    clamped = TitanAnimation_Clamp01(value);
    inverse = 1.0f - clamped;

    return 1.0f -
        (inverse * inverse * inverse);
}

float TitanAnimation_Absolute(
    float value
)
{
    return value < 0.0f ? -value : value;
}
