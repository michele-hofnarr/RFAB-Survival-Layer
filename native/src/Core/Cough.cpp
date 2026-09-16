#include "PCH.h"

#include "Core/Cough.h"

#include "Core/Disease.h"
#include "Core/Forms.h"
#include "Settings.h"

namespace RSL
{
    namespace
    {
        // Everything that can make a person cough. Hypothermia is a state of the
        // body and the tissue stress is a wound; neither belongs here, and both
        // would otherwise have the player hacking away at a frostbitten toe.
        constexpr std::string_view COUGHERS[] = {
            "BR"sv, "GW"sv, "GS"sv, "FP"sv, "CC"sv,          // ours
            "AT"sv, "RJ"sv, "WB"sv, "RA"sv, "BF"sv, "BRR"sv, "DR"sv,   // RFAB's
        };

        // Coughs per game hour, by stage. Index 0 is unused - stage 0 is well.
        constexpr float PER_HOUR[] = { 0.0f, 1.0f, 2.0f, 4.0f };

        [[nodiscard]] RE::PlayerCharacter* Player()
        {
            return RE::PlayerCharacter::GetSingleton();
        }

        [[nodiscard]] std::mt19937& Rng()
        {
            static std::mt19937 gen{ std::random_device{}() };
            return gen;
        }

        [[nodiscard]] float RandomUnit()
        {
            static std::uniform_real_distribution<float> dist{ 0.0f, 1.0f };
            return dist(Rng());
        }

        [[nodiscard]] std::size_t RandomIndex(std::size_t a_count)
        {
            return std::uniform_int_distribution<std::size_t>{ 0, a_count - 1 }(Rng());
        }

        // The worst stage the player is carrying. Not a sum: a second illness
        // does not make anyone cough twice as much, it just makes them ill.
        [[nodiscard]] std::int32_t WorstStage()
        {
            auto&        diseases = Disease::GetSingleton();
            std::int32_t worst = 0;
            for (const auto& id : COUGHERS) {
                worst = std::max(worst, diseases.Stage(id));
            }
            return std::clamp(worst, 0, 3);
        }

        // Which voice. Women of every race share a set; men share one, and orcs
        // and khajiit have their own on top of it - their lists already open
        // with the common male files, so this is one choice, not two.
        [[nodiscard]] const std::vector<RE::BGSSoundDescriptorForm*>& VoiceFor(
            RE::PlayerCharacter* a_player)
        {
            static const std::vector<RE::BGSSoundDescriptorForm*> none{};

            auto* base = a_player->GetActorBase();
            if (!base) {
                return none;
            }
            if (base->GetSex() == RE::SEX::kFemale) {
                return Forms::coughFemale;
            }

            auto* race = base->GetRace();
            if (race == Forms::raceOrc && !Forms::coughOrc.empty()) {
                return Forms::coughOrc;
            }
            if (race == Forms::raceKhajiit && !Forms::coughKhajiit.empty()) {
                return Forms::coughKhajiit;
            }
            return Forms::coughMale;
        }
    }

    Cough& Cough::GetSingleton()
    {
        static Cough singleton;
        return singleton;
    }

    void Cough::Silence()
    {
        if (_playing) {
            _handle.Stop();
            _playing = false;
        }
    }

    void Cough::Update(float a_gameHours, bool a_undead)
    {
        if (!Settings::bModEnabled || !Settings::bCoughEnabled || a_undead) {
            Silence();
            return;
        }

        auto* player = Player();
        if (!player || !player->Is3DLoaded()) {
            return;
        }

        // A cough is a quiet-moment sound. Under swinging steel it lands as
        // noise and reads as a bug.
        if (player->IsInCombat()) {
            return;
        }

        const std::int32_t stage = WorstStage();
        if (stage <= 0 || a_gameHours <= 0.0f) {
            return;
        }

        // Rate times the span. The tick is far shorter than a game hour, so
        // this is a small probability many times over rather than one big roll,
        // and that is what keeps the spacing irregular instead of metronomic.
        const float chance = PER_HOUR[stage] * Settings::fCoughRateMult * a_gameHours;
        if (RandomUnit() >= chance) {
            return;
        }

        const auto& voices = VoiceFor(player);
        auto*       audio = RE::BSAudioManager::GetSingleton();
        if (voices.empty() || !audio) {
            return;
        }
        auto* voice = voices[RandomIndex(voices.size())];

        // Building a fresh handle each time is deliberate: the handle carries
        // the sound that was picked, so reusing one would replay the same cough
        // for ever.
        Silence();
        if (!audio->BuildSoundDataFromDescriptor(_handle, voice)) {
            return;
        }
        _handle.SetObjectToFollow(player->Get3D());
        _handle.SetVolume(std::clamp(Settings::fCoughVolume, 0.0f, 1.0f));
        if (_handle.Play()) {
            _playing = true;
            logger::info("cough: stage {} ({:.2f}/h)", stage,
                PER_HOUR[stage] * Settings::fCoughRateMult);
        }
    }
}
