#include "PCH.h"

#include "Core/HitIllness.h"

namespace RSL
{
    float GutwormHungerMult()
    {
        switch (Disease::GetSingleton().Stage("GW"sv)) {
        case 3:  return 2.5f;
        case 2:  return 1.7f;
        case 1:  return 1.3f;
        default: return 1.0f;
        }
    }

    float GutwormFoodPenalty()
    {
        switch (Disease::GetSingleton().Stage("GW"sv)) {
        case 3:  return 0.8f;
        case 2:  return 0.5f;
        case 1:  return 0.25f;
        default: return 0.0f;
        }
    }

    float BrownRotSleepMult()
    {
        switch (Disease::GetSingleton().Stage("BR"sv)) {
        case 3:  return 0.7f;
        case 2:  return 0.8f;
        case 1:  return 0.9f;
        default: return 1.0f;
        }
    }
}
