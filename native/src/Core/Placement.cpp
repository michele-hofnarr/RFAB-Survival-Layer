#include "PCH.h"

#include "Core/Placement.h"

#include "Settings.h"

namespace RSL::Placement
{
    namespace
    {
        constexpr float PI = 3.14159265358979323846f;
        constexpr float DEG = 180.0f / PI;
        constexpr float RAD = PI / 180.0f;

        // How far above and below the reference height to look for a surface.
        constexpr float PROBE_UP = 200.0f;
        constexpr float PROBE_DOWN = 600.0f;

        // Where the "is the way clear" ray runs, above the player's feet.
        constexpr float CHEST = 90.0f;

        struct Ground
        {
            float        z{ 0.0f };
            RE::NiPoint3 normal{ 0.0f, 0.0f, 1.0f };
            bool         hit{ false };
            bool         fromRay{ false };   // false means the terrain fallback
        };

        // What is under this point, by asking havok - both the height and the
        // surface normal.
        //
        // GetLandHeight knows about the landscape and nothing else, so on a
        // road, a bridge, a rock or any built floor it answers with the terrain
        // underneath and the object ends up buried or hanging. The terrain
        // answer stays as the fallback for when the ray misses entirely, and
        // then the surface counts as flat, because terrain alone cannot say
        // otherwise.
        [[nodiscard]] Ground GroundAt(float a_x, float a_y, float a_refZ)
        {
            Ground out;

            auto* tes = RE::TES::GetSingleton();
            if (!tes) {
                return out;
            }

            const float     scale = RE::bhkWorld::GetWorldScale();
            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(
                a_x * scale, a_y * scale, (a_refZ + PROBE_UP) * scale, 0.0f);
            pick.rayInput.to = RE::hkVector4(
                a_x * scale, a_y * scale, (a_refZ - PROBE_DOWN) * scale, 0.0f);
            pick.rayInput.filterInfo =
                static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) << 16;

            tes->Pick(pick);
            if (pick.rayOutput.HasHit()) {
                const float span = PROBE_UP + PROBE_DOWN;
                out.z = a_refZ + PROBE_UP - span * pick.rayOutput.hitFraction;

                // The normal comes back with the hit. This is the whole reason
                // to prefer one ray over three: Campfire solved the slope from
                // three sampled heights because Papyrus had no rays to cast,
                // and three samples a hundred units apart pick up every rock
                // and step between them. On broken ground that produced tilts
                // pinned at the 25-degree clamp with the roll swinging from one
                // extreme to the other - the bedroll lay at a different crazy
                // angle every time it was put down. One normal has none of that
                // noise in it.
                float n[4]{};
                _mm_storeu_ps(n, pick.rayOutput.normal.quad);
                out.normal = { n[0], n[1], n[2] };

                const float len = out.normal.Length();
                if (len > 1e-4f) {
                    out.normal /= len;
                } else {
                    out.normal = { 0.0f, 0.0f, 1.0f };
                }
                out.hit = true;
                out.fromRay = true;
                return out;
            }

            const RE::NiPoint3 at{ a_x, a_y, a_refZ };
            float              height = 0.0f;
            if (tes->GetLandHeight(at, height)) {
                out.z = height;
                out.hit = true;
            }
            return out;
        }

        // Campfire's terrain solve, ported from _Camp_ObjectPlacementThreadManager
        // (GetTerrainRotation, and the three probes that feed it).
        //
        // The part that matters, and the part the first port got wrong: the
        // three probes are laid out in WORLD axes, not rotated by the player's
        // heading.
        //
        //     A = centre + ( 0.0, -43.3)
        //     B = centre + (-50.0, +43.3)
        //     C = centre + (+50.0, +43.3)
        //
        // So B->C measures the slope along world X and gives the Y angle, and
        // A against the midpoint of B and C measures it along world Y and gives
        // the X angle. The heading goes on Z afterwards and does not enter into
        // either. That is how the engine reads those two angles, and computing
        // them in the player's own frame instead - which is what this did -
        // tilts the object in a direction that swings with which way the player
        // happens to be facing.
        constexpr float PROBE_A_Y = -43.3f;
        constexpr float PROBE_BC_Y = 43.3f;
        constexpr float PROBE_BC_X = 50.0f;

        // The two runs the right triangles are solved over: B to C, and A to
        // the midpoint of B and C.
        constexpr float RUN_ROLL = 100.0f;
        constexpr float RUN_PITCH = 86.6f;

        [[nodiscard]] float TiltFrom(float a_rise, float a_run)
        {
            if (a_rise <= 0.0f) {
                return 0.0f;
            }
            const float hyp = std::sqrt(a_rise * a_rise + a_run * a_run);
            return std::asin(a_rise / hyp) * DEG;
        }
    }

    bool BlockedInFront(float a_distance)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tes = RE::TES::GetSingleton();
        if (!player || !tes || !player->Is3DLoaded()) {
            return false;
        }

        const auto  from = player->GetPosition();
        const float heading = player->GetAngleZ();
        const float scale = RE::bhkWorld::GetWorldScale();
        const float z = from.z + CHEST;

        RE::bhkPickData pick;
        pick.rayInput.from =
            RE::hkVector4(from.x * scale, from.y * scale, z * scale, 0.0f);
        pick.rayInput.to = RE::hkVector4(
            (from.x + std::sin(heading) * a_distance) * scale,
            (from.y + std::cos(heading) * a_distance) * scale,
            z * scale, 0.0f);
        pick.rayInput.filterInfo =
            static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) << 16;

        tes->Pick(pick);
        if (!pick.rayOutput.HasHit()) {
            return false;
        }

        logger::info("placement: blocked {:.0f} units ahead of {:.0f}",
            a_distance * pick.rayOutput.hitFraction, a_distance);
        return true;
    }

    Spot InFront(float a_distance, float a_maxTiltDegrees)
    {
        Spot spot;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return spot;
        }

        const auto origin = player->GetPosition();

        // Radians already. GetAngleZ returns data.angle.z untouched, and
        // CommonLibSSE itself calls rad_to_deg on it elsewhere - Papyrus's
        // GetAngleZ is the one that converts. Treating this as degrees puts
        // every placed object at a wrong angle in a wrong direction, and the
        // maths looks perfectly reasonable either way.
        const float heading = player->GetAngleZ();
        spot.heading = heading;

        // Where it goes is in front of the player; how it lies is not.
        const float cx = origin.x + std::sin(heading) * a_distance;
        const float cy = origin.y + std::cos(heading) * a_distance;
        spot.position = { cx, cy, origin.z };

        const Ground gA = GroundAt(cx, cy + PROBE_A_Y, origin.z);
        const Ground gB = GroundAt(cx - PROBE_BC_X, cy + PROBE_BC_Y, origin.z);
        const Ground gC = GroundAt(cx + PROBE_BC_X, cy + PROBE_BC_Y, origin.z);

        if (!gA.hit || !gB.hit || !gC.hit) {
            // Nothing under the spot at all. v0.4.0 placed everything at the
            // player's feet unconditionally, so this is the old behaviour
            // rather than a failure.
            if (Settings::bDebugLog) {
                logger::info("placement: no ground under the spot, using the player's feet");
            }
            return spot;
        }

        const float zA = gA.z;
        const float zB = gB.z;
        const float zC = gC.z;

        // Roll, from B against C across the full side. Campfire's signs: B
        // higher rolls one way, C higher the other.
        float roll = 0.0f;
        if (zB > zC) {
            roll = -TiltFrom(zB - zC, RUN_ROLL);
        } else if (zC > zB) {
            roll = TiltFrom(zC - zB, RUN_ROLL);
        }
        const float midBC = (zB + zC) * 0.5f;

        // Pitch, from A against that midpoint.
        float pitch = 0.0f;
        if (zA > midBC) {
            pitch = TiltFrom(zA - midBC, RUN_PITCH);
        } else if (zA < midBC) {
            pitch = -TiltFrom(midBC - zA, RUN_PITCH);
        }

        spot.pitch = std::clamp(pitch, -a_maxTiltDegrees, a_maxTiltDegrees) * RAD;
        spot.roll = std::clamp(roll, -a_maxTiltDegrees, a_maxTiltDegrees) * RAD;

        // Halfway between the front sample and the midpoint of the back two,
        // which is the centre of the triangle's A-to-BC line - the point the
        // object is actually standing on.
        spot.position.z = (zA + midBC) * 0.5f;
        spot.grounded = true;

        if (Settings::bDebugLog) {
            logger::info("placement: z {:.0f} ({:+.0f} from the player's feet, {}) "
                         "pitch {:.1f} roll {:.1f} deg (A {:.0f} B {:.0f} C {:.0f})",
                spot.position.z, spot.position.z - origin.z,
                gA.fromRay ? "ray" : "terrain",
                spot.pitch * DEG, spot.roll * DEG, zA, zB, zC);
        }
        return spot;
    }
}
