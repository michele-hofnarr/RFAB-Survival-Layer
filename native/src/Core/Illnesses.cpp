#include "PCH.h"

#include "Core/Illnesses.h"

#include "Core/ColdIllness.h"
#include "Core/Forms.h"
#include "Core/HitIllness.h"
#include "Core/LesionIllness.h"
#include "Core/RfabIllness.h"

namespace RSL
{
    namespace
    {
        // RFAB's seven in Forms order: AT RJ WB RA BF BRR DR. The first six are
        // base-game diseases the Peryite blessing freezes at stage 1;
        // Dragonborn's Droops is not covered by it.
        //
        // This used to be `a_index <= 5` inside the update loop - a magic
        // number in the shared code, standing for a fact about six particular
        // records. It is passed to the illness that owns it now.
        constexpr int BLESSED_COUNT = 6;
    }

    Illnesses& Illnesses::GetSingleton()
    {
        static Illnesses singleton;
        return singleton;
    }

    void Illnesses::Build()
    {
        _all.clear();
        _lesion = nullptr;

        // ORDER IS THE TICK'S ORDER, kept exactly: the cold, the four caught by
        // a hit, RFAB's seven, the lesions. Illnesses read each other - the
        // lesions ask after hypothermia, the cough asks after all of them - so
        // the order in which they settle within one pass is observable.
        _all.push_back(std::make_unique<ColdIllness>(Forms::commonCold));

        for (const auto& dz : Forms::hitDisease) {
            _all.push_back(std::make_unique<HitIllness>(dz));
        }

        for (int i = 0; i < 7; ++i) {
            _all.push_back(std::make_unique<RfabIllness>(Forms::rfabDisease[i],
                i < BLESSED_COUNT));
        }

        auto lesion = std::make_unique<LesionIllness>(Forms::elemLesion);
        _lesion = lesion.get();
        _all.push_back(std::move(lesion));

        std::size_t ready = 0;
        for (const auto& illness : _all) {
            if (illness->Ready()) {
                ++ready;
            }
        }
        logger::info("{} illnesses built, {} with their records resolved", _all.size(),
            ready);
    }

    void Illnesses::Update(const Tick& a_tick)
    {
        for (const auto& illness : _all) {
            illness->Update(a_tick);
        }
    }

    void Illnesses::ClearAll()
    {
        for (const auto& illness : _all) {
            if (illness->Ready()) {
                illness->Clear();
            }
        }
    }

    Illness* Illnesses::Find(std::string_view a_id)
    {
        for (const auto& illness : _all) {
            if (illness->Id() == a_id) {
                return illness.get();
            }
        }
        return nullptr;
    }
}
