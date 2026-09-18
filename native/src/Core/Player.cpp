#include "PCH.h"

#include "Core/Player.h"

namespace RSL
{
    RE::PlayerCharacter* Player()
    {
        return RE::PlayerCharacter::GetSingleton();
    }

    float PlayerAV(RE::ActorValue a_value)
    {
        auto* player = Player();
        // Through AsActorValueOwner by name: PlayerCharacter inherits the
        // interface more than once and the call is ambiguous without saying
        // which.
        auto* owner = player ? player->AsActorValueOwner() : nullptr;
        return owner ? owner->GetActorValue(a_value) : 0.0f;
    }

    float PlayerBaseAV(RE::ActorValue a_value)
    {
        auto* player = Player();
        auto* owner = player ? player->AsActorValueOwner() : nullptr;
        return owner ? owner->GetPermanentActorValue(a_value) : 0.0f;
    }

    float DiseaseResist()
    {
        return PlayerAV(RE::ActorValue::kResistDisease);
    }
}
