#include "PCH.h"

#include "Core/Random.h"

#include <random>

namespace RSL
{
    namespace
    {
        // Everything that rolls here runs on the game thread - the needs tick
        // and the event sinks alike - so one generator with no lock is enough,
        // and saying so is cheaper than a mutex nobody needs.
        [[nodiscard]] std::mt19937& Gen()
        {
            static std::mt19937 gen{ std::random_device{}() };
            return gen;
        }
    }

    float RollPercent()
    {
        static std::uniform_real_distribution<float> dist{ 0.0f, 100.0f };
        return dist(Gen());
    }

    float RollUnit()
    {
        static std::uniform_real_distribution<float> dist{ 0.0f, 1.0f };
        return dist(Gen());
    }

    std::size_t RollIndex(std::size_t a_count)
    {
        if (a_count <= 1) {
            return 0;
        }
        return std::uniform_int_distribution<std::size_t>{ 0, a_count - 1 }(Gen());
    }
}
