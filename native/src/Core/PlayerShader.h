#pragma once

// One effect shader on the player: start it, stop it, and nothing else.
//
// WHY THIS IS NOT PAPYRUS ANY MORE. It used to be - EffectShader.Play and .Stop
// through DispatchMethodCall2, because CommonLibSSE binds neither by name. That
// route cost a reproducible crash: a dispatched call is queued rather than run,
// so the arguments were freed while the VM still held them and the fault landed
// a frame later. TESObjectREFR::ApplyEffectShader is the engine's own entry
// point and it runs now.
//
// ===========================================================================
// TESObjectREFR::ApplyEffectShader DOES NOT RETURN THE EFFECT ON 1.5.97.
//
// CommonLibSSE declares it as returning ShaderReferenceEffect*. It does not.
// Disassembled from SkyrimSE.exe 1.5.97 at the address the library resolves
// (id 19446 -> RVA 0x29CB80), the function allocates 0x138 bytes, runs the
// constructor, and then TAIL-JUMPS away - so whatever the jumped-to function
// leaves in RAX is what the caller receives:
//
//   0x29CB80 ApplyEffectShader
//     -> jmp 0x55BF30
//          AL == 0 -> jmp [vtable+0] with edx=1   the DELETING destructor:
//                                                 the effect is freed here
//          AL != 0 -> jmp 0x6DD170
//                       -> jmp 0x5C62A0  insert into a list
//                       -> jmp 0x6E0D10  insert into another list
//
// On no path does anything put the effect in RAX deliberately. The function is
// void in everything but its declaration: create, initialise, register - or
// destroy.
//
// That one fact is the whole history of this file:
//
//   1. Keep the returned pointer and write finished through it - wrote through
//      a value that was never the effect.
//   2. Keep it, and look for THAT pointer in ProcessLists before writing. It
//      was never found, not once, so the crust could not be taken off at all -
//      not by warming up, not by the MCM switch.
//   3. Keep it in a NiPointer so holding it would be safe. The game crashed on
//      the first application, before a line could be logged: taking a reference
//      was the first time anything dereferenced that value.
//
// So the effect is found rather than received - not as a fallback, but because
// the engine hands the caller nothing to hold.
// ===========================================================================
//
// WHICH INSTANCE IS OURS is then the only question left, and it is answered
// exactly rather than by resemblance. These are vanilla shaders - 000DC20D is
// frost magic's own - so "an instance of this shader on the player" is not good
// enough; stopping one this mod never started would end a spell's own visual.
//
// The disassembly above settles it: registration happens INSIDE the call, in
// the tail chain, so the new effect is in the list by the time the call
// returns. Take the set of matching instances before, call, look again, and the
// one that was not there before is ours. After that it is identity, not
// matching.
//
// The pointer is still never dereferenced on its own: Find() returns it only
// after seeing it in the engine's live list, so an effect the engine has since
// reaped is simply not found.

namespace RSL
{
    class PlayerShader
    {
    public:
        // Start it, for as long as it takes. False means the player has no model
        // to hang it on - the state a fast travel passes through - so the caller
        // can leave its own flag alone and try again on the next pass.
        bool Apply(RE::TESEffectShader* a_shader)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !a_shader || !player->Is3DLoaded()) {
                return false;
            }
            if (Find(a_shader)) {
                return true;   // ours and still running; a second apply stacks
            }

            std::vector<const void*> before;
            ForEachOn(a_shader, [&before](RE::ShaderReferenceEffect& a_effect) {
                before.push_back(&a_effect);
            });

            // -1 is the engine's own "until told otherwise". The return value is
            // deliberately dropped - see the note above.
            player->ApplyEffectShader(a_shader, -1.0f);

            _instance = nullptr;
            ForEachOn(a_shader, [&](RE::ShaderReferenceEffect& a_effect) {
                if (std::find(before.begin(), before.end(), &a_effect) == before.end()) {
                    _instance = &a_effect;
                }
            });
            return _instance != nullptr;
        }

        void Stop(RE::TESEffectShader* a_shader)
        {
            if (auto* effect = Find(a_shader)) {
                effect->finished = true;
            }
            _instance = nullptr;
        }

        // A load: the engine's temp effects went with the last game.
        void Forget() { _instance = nullptr; }

    private:
        // Our instance, but only if the engine still has it. Never returns a
        // pointer that has not just been seen alive in the list.
        [[nodiscard]] RE::ShaderReferenceEffect* Find(RE::TESEffectShader* a_shader) const
        {
            if (!_instance) {
                return nullptr;
            }
            RE::ShaderReferenceEffect* found = nullptr;
            ForEachOn(a_shader, [&](RE::ShaderReferenceEffect& a_effect) {
                if (&a_effect == _instance) {
                    found = &a_effect;
                }
            });
            return found;
        }

        // Every instance of a_shader playing on the player.
        //
        // ForEachShaderEffect rather than a hand-rolled walk over
        // ForEachMagicTempEffect: it is the same list either way, but it casts
        // with BSTempEffect::As, and an earlier attempt that cast with
        // netimmerse_cast matched nothing.
        template <class F>
        void ForEachOn(RE::TESEffectShader* a_shader, F a_fn) const
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!lists || !player || !a_shader) {
                return;
            }

            const auto handle = player->GetHandle();
            lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect& a_effect) {
                if (a_effect.effectData == a_shader && a_effect.target == handle) {
                    a_fn(a_effect);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        RE::ShaderReferenceEffect* _instance{ nullptr };
    };
}
