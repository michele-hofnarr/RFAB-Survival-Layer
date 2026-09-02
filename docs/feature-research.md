# Feature notes

Three features designed and shipped. Design decisions and their rationale kept
here; mechanics are in README.md.

---

## 1. Combat ×5 fatigue — SHIPPED

`CombatMult()` in `_RSL_Controller`: `GetCombatState() == 1` → `CombatFatigueMult`
(GLOB, default 5), else 1.0. Multiplies the `dtHours` term in `AdvanceSleep` and
the `dtHours * GwHungerMult()` term in `AdvanceHunger`.

- Only state 1 (in combat), not 2 ("searching") — searching can linger.
- One GLOB drives both axes; MCM slider on the Сон page (1–15), help text notes
  it hits hunger too.
- `OnSleepStop`'s hunger replay runs `CombatMult()` at post-wake state (normally
  0) — fine.

## 2. Frost damage cools / fire damage warms — SHIPPED (magic only)

`NoteElemHit(MagicEffect)` from `OnMagicEffectApply` (already wired on the monitor
ability). Keyword check `KwMagicDamageFrost` / `KwMagicDamageFire` (resolved by
EditorID in the generator, baked into `_RSL_Forms.psc`). Frost → `+FrostHitCold`,
fire → `−FireHitWarm` (GLOBs, default 4 each, 0 disables).

- **Throttle:** `NoteElemHit` accepts one hit per `ELEM_MIN_GAP` (0.5) real
  seconds (`K_ELEMT` timestamp via `Utility.GetCurrentRealTime()`) — a frost
  cloak / channelled `Flames` fires the event dozens of times a second, this
  caps the real rate at ~2/s regardless. Accepted hits queue into `K_ELEMACC`
  (signed, clamped ±20); `ApplyElemHits()` in `OnUpdate` (before `AdvanceCold`)
  folds it whole into `cold` — the rate limit already bounds it to ~4/tick.
- **Magnitude:** default 2 per hit (was 4 — felt like a sledgehammer next to the
  ~0.1–0.5/tick the bar otherwise moves). `WarmupMult` does NOT touch these -
  `ApplyElemHits` writes straight to `K_COLD`, bypassing `AdvanceCold`.
- **Resist scaling:** `d *= ResistFactor(av)` = `clamp(1 - AV/100, 0, 2)` on
  `FrostResist` / `FireResist`. Full immunity → no chill/warmth, weakness → up to
  2×. Consistent with `Mitigation()` already folding `FrostResist` into cold
  defense.
- **No magnitude scaling:** `OnMagicEffectApply` doesn't expose the applied
  damage number. Flat per-hit bump; the per-tick cap makes a barrage and one big
  Ice Storm behave similarly.
- **`IsHostile()` dropped:** not a function on `MagicEffect` in this Papyrus
  setup. The `MagicDamage*` keywords are specific enough on their own.
- **Fire-warm exploit accepted** (user decision): standing in a fire trap or
  self-casting Flames warms you, but burning is HP loss every tick — a real
  trade, not a free heater. No floor-check code.
- **Scope:** magic only for now. Enchanted weapons (frost/fire enchant via
  `OnHit` → `akSource` enchantment inspection) deferred.
- Cleared in `TeardownAll` (mod-off).

## 3. Altitude ramp false-positive in Riften — SHIPPED

`Severity()`: `bool coldClimate = (wcls == 3) || SnowClimateHere()`, computed once,
now gates **both** the snow floor and the altitude ramp.

- Riften: `RegionRift` base 10, player Z ≈ 11117 → old `t ≈ 0.52` → `sev ≈ 57`
  (Winterhold-tier) purely from absolute worldspace Z. Rift region lists no snow
  weather → `coldClimate` false → ramp skipped, `sev` stays 10.
- Bonus: also fixes the opposite failure — a snowy low peak (Ветреный Пик ~3700 <
  `AltitudeLow`) is in a snow region, so `coldClimate` is true and the ramp now
  applies there too.
- **Caveat to watch:** confirm real mountain regions (High Hrothgar / Throat of
  the World / Falkreath's southern peaks) list a snow-class weather so genuine
  peaks keep the ramp. They almost certainly do.
