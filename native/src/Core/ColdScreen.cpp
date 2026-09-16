#include "PCH.h"

#include "Core/ColdScreen.h"

#include "Settings.h"

namespace RSL
{
    namespace
    {
        constexpr std::string_view SKYRIM = "Skyrim.esm"sv;
    }

    // Both stock records. The editor id is carried so the log says what is on
    // screen in words rather than in hex.
    const ColdScreen::Entry ColdScreen::ENTRIES[] = {
        { SKYRIM, 0x0B97F7, "ISMDinCloudBlurStatic" },
        { SKYRIM, 0x044F3B, "FXTimeTravelImodStatic" },
    };

    const int ColdScreen::COUNT =
        static_cast<int>(sizeof(ENTRIES) / sizeof(ENTRIES[0]));

    ColdScreen& ColdScreen::GetSingleton()
    {
        static ColdScreen singleton;
        return singleton;
    }

    void ColdScreen::Resolve()
    {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            return;
        }
        _resolved = true;

        for (int i = 0; i < COUNT; ++i) {
            const auto& e = ENTRIES[i];
            auto*       form = handler->LookupForm(e.id, e.file);

            // As<T>(), never LookupForm<T>: FORMTYPE on a base class matches
            // nothing. See the note in Core/Forms.cpp - it cost a whole build.
            _imod[i] = form ? form->As<RE::TESImageSpaceModifier>() : nullptr;
            if (!_imod[i]) {
                logger::error("cold screen effect {} ({:#08x}) not found in {}",
                    e.edid, e.id, e.file);
            }
        }
    }

    void ColdScreen::Update(float a_cold)
    {
        if (!_resolved) {
            Resolve();
        }

        // How far into the window the cold bar is. Lower reserve is colder, so
        // it counts down: nothing at BEGIN_AT, everything at FULL_AT.
        float t = 0.0f;
        if (Settings::bModEnabled && Settings::bColdScreenEnabled) {
            t = std::clamp((BEGIN_AT - a_cold) / (BEGIN_AT - FULL_AT), 0.0f, 1.0f);
        }

        const int want = static_cast<int>(t * BUCKETS + 0.5f);

        for (int i = 0; i < COUNT; ++i) {
            if (!_imod[i] || want == _bucket[i]) {
                continue;
            }

            // Stop first, always. Trigger stacks, so skipping this leaves the
            // previous strength running underneath the new one.
            RE::ImageSpaceModifierInstanceForm::Stop(_imod[i]);
            if (want > 0) {
                RE::ImageSpaceModifierInstanceForm::Trigger(
                    _imod[i], static_cast<float>(want) / BUCKETS, nullptr);
            }
            _bucket[i] = want;
            logger::info("cold screen {} -> {:.2f}", ENTRIES[i].edid,
                static_cast<float>(want) / BUCKETS);
        }
    }

    void ColdScreen::ClearAll()
    {
        for (int i = 0; i < COUNT; ++i) {
            if (_bucket[i] > 0 && _imod[i]) {
                RE::ImageSpaceModifierInstanceForm::Stop(_imod[i]);
            }
            _bucket[i] = 0;
        }
    }
}
