#include "PCH.h"

#include "Core/RfabPatch.h"

namespace RSL
{
    namespace
    {
        constexpr std::string_view RFAB = "RFAB.esp"sv;

        // The spell tome for "Control Weather". The spell itself is retuned to
        // master tier in the .esp (MGEF Minimum Skill Level 100, SPEL half-cost
        // perk AlterationMaster100) and shows correctly in the magic menu; only
        // the book kept its novice tag.
        constexpr RE::FormID BOOK_CONTROL_WEATHER = 0x089704;

        // RFAB tags every one of its tomes with a bracketed tier - the novice
        // and master words in square brackets ahead of the spell name. So the
        // fix is the tag, not the name: swap what is inside the brackets and
        // leave the rest alone.
        //
        // The replacement is spelled out byte by byte, and the bytes are UTF-8.
        //
        // The first attempt wrote CP1251, on the reasoning that RFAB.esp is not
        // a localised plugin and so its strings must be in the game's ANSI
        // codepage. They are not: the record holds UTF-8 on disk - its FULL
        // subrecord starts 5B D0 9D D0 BE D0 B2 for "[Nov..." in Cyrillic - and
        // that is what GetFullName hands back. The CP1251 version wrote
        // mojibake, and the log showed it plainly next to the original it had
        // just printed correctly.
        //
        // D0 9C D0 B0 D1 81 D1 82 D0 B5 D1 80 is the Russian word for "master".
        // Written as escapes rather than as source text so that no compiler
        // switch and no editor can reinterpret it.
        constexpr std::string_view MASTER_TAG =
            "[\xD0\x9C\xD0\xB0\xD1\x81\xD1\x82\xD0\xB5\xD1\x80]"sv;

        void RetierControlWeatherTome()
        {
            auto* handler = RE::TESDataHandler::GetSingleton();
            if (!handler) {
                return;
            }

            auto* form = handler->LookupForm(BOOK_CONTROL_WEATHER, RFAB);
            auto* book = form ? form->As<RE::TESObjectBOOK>() : nullptr;
            if (!book) {
                logger::warn("control-weather tome ({:#08x}) not found in {} - "
                             "did RFAB reindex it?",
                    BOOK_CONTROL_WEATHER, RFAB);
                return;
            }

            const std::string_view name = book->GetFullName();
            if (name.empty()) {
                return;
            }

            // Only ever rewrite a tag that is there. If RFAB changes the naming
            // convention, this does nothing rather than producing a name in two
            // conventions at once.
            const auto close = name.find(']');
            if (name.front() != '[' || close == std::string_view::npos) {
                logger::warn("control-weather tome is named \"{}\" - no tier tag, left alone",
                    name);
                return;
            }

            if (name.substr(0, close + 1) == MASTER_TAG) {
                return;   // already right, nothing to say
            }

            std::string fixed{ MASTER_TAG };
            fixed.append(name.substr(close + 1));
            book->fullName = fixed;
            logger::info("control-weather tome retiered: \"{}\" -> \"{}\"", name, fixed);
        }
    }

    void RfabPatch::Apply()
    {
        RetierControlWeatherTome();
    }
}
