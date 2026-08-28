#ifndef PROJECT_TITAN_ANIMATION_H
#define PROJECT_TITAN_ANIMATION_H

float TitanAnimation_Approach(
    float current,
    float target,
    float factor
);

float TitanAnimation_Clamp01(
    float value
);

float TitanAnimation_SmoothStep(
    float value
);

float TitanAnimation_EaseOutCubic(
    float value
);

float TitanAnimation_Absolute(
    float value
);

#endif
