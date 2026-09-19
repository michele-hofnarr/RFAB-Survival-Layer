#pragma once

// The screen while freezing: two vanilla image space modifiers over the last
// third of the cold bar.
//
//     ISMDinCloudBlurStatic     the edges go soft and dark
//     FXTimeTravelImodStatic    the picture fades out
//
// Chosen, not guessed. v0.4.0 layered three of these and none did what it was
// picked for - its desaturate only darkened, its tint did nothing at all - so
// seventeen candidates and a camera shake went onto a debug page as checkboxes
// and were looked at one at a time and in pairs. These two are what survived,
// and the window is what looked right with them. The catalogue and its
// checkboxes are gone: keeping them would be keeping the question open after
// it was answered.
//
// Two things are true of every image space modifier and worth stating once:
//
//   Trigger() STACKS. Applying at a new strength without stopping first leaves
//   the old instance running, so a bucket change is always stop-then-trigger,
//   and re-triggering on every tick would flicker and eventually strand one on
//   screen. Hence the 5% quantisation - the same v0.4.0 uses, for the same
//   reason.
//
//   Nothing here is authored. Both are stock records, scaled by the strength
//   argument.

namespace RSL
{
    class ColdScreen
    {
    public:
        static ColdScreen& GetSingleton();

        // a_cold is the reserve, 0..1, and lower is colder.
        void Update(float a_cold);

        // Every effect off, now. The one path off the screen: v0.4.0's worst
        // bug was the teardown branch never reaching the code that removed
        // these, and leaving the player looking through a frozen filter with
        // the mod switched off.
        void ClearAll();

        // A load: the engine's image space modifier instances went with the
        // last game, and the bucket we remember describes THEM.
        //
        // Without this the screen stayed clear at a cold bar that should have
        // dimmed it, for as long as the loaded game happened to sit in the
        // same 5% bucket the last one ended on - Update compares against the
        // bucket and nothing else, so an unchanged number means no work to do.
        // Every other cache of engine state here has one of these; this was
        // the one that did not.
        void Forget();

        // The window this runs in, in bar fractions: nothing at BEGIN_AT,
        // full strength at FULL_AT, which is the floor - so the last third
        // of the bar is spent watching the picture go.
        //
        // Public because the temperature icon's coldest step is this same
        // line - it means "this place will push you into the screen
        // effects" - and a second copy of 0.30 elsewhere could drift away
        // from this one.
        static constexpr float BEGIN_AT = 0.30f;
        static constexpr float FULL_AT = 0.00f;

    private:
        struct Entry
        {
            std::string_view file;
            RE::FormID       id;
            const char*      edid;
        };

        static const Entry ENTRIES[];
        static const int   COUNT;

        // Strength runs on a 5% grid: 0 is off, 20 is full.
        static constexpr int BUCKETS = 20;

        void Resolve();

        bool                       _resolved{ false };
        RE::TESImageSpaceModifier* _imod[4]{};
        int                        _bucket[4]{};
    };
}
