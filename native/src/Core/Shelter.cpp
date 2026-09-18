#include "PCH.h"

#include "Core/Shelter.h"

#include "Core/Player.h"
#include "Settings.h"

namespace RSL
{
    Shelter& Shelter::GetSingleton()
    {
        static Shelter singleton;
        return singleton;
    }

    void Shelter::Invalidate()
    {
        _valid = false;
    }

    bool Shelter::CastUp()
    {
        auto* player = Player();
        auto* tes = RE::TES::GetSingleton();
        if (!player || !tes || !player->Is3DLoaded()) {
            return false;
        }

        const auto  from = player->GetPosition();
        const float scale = RE::bhkWorld::GetWorldScale();

        RE::bhkPickData pick;
        pick.rayInput.from = RE::hkVector4(
            from.x * scale, from.y * scale, (from.z + HEAD) * scale, 0.0f);
        pick.rayInput.to = RE::hkVector4(
            from.x * scale, from.y * scale, (from.z + HEAD + REACH) * scale, 0.0f);

        // Line of sight is the right layer for "is anything in the way": it is
        // what the engine itself uses to ask whether one thing can see another,
        // so it already ignores the things that should not block a view of the
        // sky and stops at the things that should.
        pick.rayInput.filterInfo =
            static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) << 16;

        tes->Pick(pick);
        return pick.rayOutput.HasHit();
    }

    bool Shelter::Overhead()
    {
        auto* player = Player();
        if (!player) {
            return false;
        }

        // An interior is a roof by definition, and asking havok about one is
        // both wasteful and liable to answer with the ceiling of the room above.
        auto* cell = player->GetParentCell();
        if (cell && cell->IsInteriorCell()) {
            _overhead = true;
            _valid = true;
            _cell = cell;
            _taken = std::chrono::steady_clock::now();
            return true;
        }

        // Walking through a doorway is exactly when a cached answer is wrong,
        // so a cell change forces a fresh look rather than waiting for the
        // timer to come round.
        const auto now = std::chrono::steady_clock::now();
        const bool stale =
            !_valid || cell != _cell ||
            std::chrono::duration<float>(now - _taken).count() >= REFRESH_SECONDS;

        if (stale) {
            const bool before = _overhead;
            _overhead = CastUp();
            _valid = true;
            _cell = cell;
            _taken = now;

            if (Settings::bDebugLog && _overhead != before) {
                logger::info("shelter: {}", _overhead ? "under cover" : "open sky");
            }
        }

        return _overhead;
    }
}
