# Releasing

Build artifacts (`.esp`, `.pex`, `.swf`, `.dll`, `.bin`) are **not** tracked in git —
they are reproduced from source and uploaded straight to a GitHub Release.

## 1. Regenerate the plugin

In SSEEdit / xEdit: **Apply Script → `RFAB_SurvivalLayer_01_Records`**
(source: `SSEEdit_Scripts/`, deployed to the Edit Scripts folder by
`SSEEdit_Scripts/deploy.sh` — run it first, or the generator reads a stale
strings file). Expect `ПРОБЛЕМ: 0`. This rewrites:

- `RFAB_SurvivalLayer.esp` — **tracked**, and it is the input as well as the
  output: the generator edits the plugin that is there. That is what keeps a
  record's FormID the same from one release to the next, which is what lets a
  player's save survive an update. Never run the generator against a missing
  or hand-cleared plugin.
- `native/src/Core/FormIDs.h` — **tracked** (generated code), commit it

Move the regenerated `.esp` out of MO2's Overwrite into the mod folder.

**Then run the generator a second time and check that `FormIDs.h` did not
change.** Every builder reuses its record by EditorID, so a second run must be
a no-op; a diff means one of them started handing out new ids again, and
shipping that breaks every save made on the previous release. `git diff
native/src/Core/FormIDs.h` is the whole test.

## 2. Build the native plugin

```
cmake --build native/build
```

Needs `CommonLibSSEPath` and `VCPKG_ROOT` set, and a VS2019 x64 environment
(`vcvars64.bat`) — without it the linker cannot find `Version.lib`. Produces
`SKSE/Plugins/_RSL_Core.dll` via the post-build copy. **The game must be
closed**, or the copy fails silently.

Always rebuild after step 1: `FormIDs.h` was just regenerated.

## 3. Regenerate the derived files

```
python tools/make_mcm.py            # MCM/Config/... config.json + settings.ini
python tools/build_translations.py  # Interface/Translations/*.txt
python tools/make_widget_assets.py  # widget/generated/vignette.png
python tools/make_assets_swf.py     # Interface/RSL_SurvivalHUD.swf
python tools/bake_climate.py        # SKSE/Plugins/_RSL_Climate.bin
```

`bake_climate.py` reads `Skyrim.esm` and `docs/control_points.xlsx` and takes a
few minutes. Only needed when the workbook changed — the temperatures in it are
hand-set and are the only part of the climate nothing else can reproduce. It
also redraws `docs/img/map_baked.png`, which is worth a look: the field is
easier to judge by eye than by numbers.

The first two are **tracked** — commit them. `make_mcm.py` refuses to write if
the menu and `Settings.h` have drifted, so a non-zero exit is a real problem.

## 4. Compile Papyrus (only if `_RSL_MCM.psc` changed, which it does not)

```
_build.bat _RSL_MCM
```

One empty script, and only because MCM Helper will not register a menu without
a quest carrying a `MCM_ConfigBase` subclass.

## 5. Sanity pass

- `SSEEdit_Scripts/RFAB_Validate_Deps.pas` (Apply Script) → all PASS.
- Load a save, smoke-test sleep / eat / cold / a disease / hypothermia.

## 6. Package and publish

The release archive is the mod folder as MO2 installs it — everything except
`.git/`, `SSEEdit_Scripts/`, `native/`, `widget/`, `tools/`, `docs/`, and the
`*.md` files. Zip it, then:

```
gh release create vX.Y --title "vX.Y" --notes "..." \
  RFAB_SurvivalLayer.zip
```

Or attach the loose `.esp` + `SKSE/` + `Interface/` + `scripts/` + `MCM/` if
the archive is assembled elsewhere.

Tested against **RFAB SE XI - Respect Edition [ver. 09.07.2026]** — note the
tested-on version in the release notes.
