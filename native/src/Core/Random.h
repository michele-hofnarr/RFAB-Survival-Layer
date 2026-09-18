#pragma once

// One generator for the whole mod.
//
// There were five, each a function-local static mt19937 seeded from
// random_device in its own translation unit: the illnesses had four between
// them and the cough had its own. Five independent streams, five seedings, and
// nowhere to stand if a run ever has to be made repeatable.

namespace RSL
{
    // A fresh number in [0, 100), to compare against a chance stated the way
    // the settings state them - as a percentage:
    //
    //     if (RollPercent() < chance) ...
    [[nodiscard]] float RollPercent();

    // The same in [0, 1), for chances that are already fractions.
    [[nodiscard]] float RollUnit();

    // A position in [0, a_count). Returns 0 for an empty range rather than
    // stepping off the end, so a caller with nothing to choose from gets an
    // answer it can throw away instead of undefined behaviour.
    [[nodiscard]] std::size_t RollIndex(std::size_t a_count);
}
