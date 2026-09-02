# Releasing

Build artifacts (`.esp`, `.pex`, `.swf`) are **not** tracked in git — they are
reproduced from source and uploaded straight to a GitHub Release.

## 1. Regenerate the plugin

In SSEEdit / xEdit: **Apply Script → `RFAB_SurvivalLayer_01_Records`**
(source: `SSEEdit_Scripts/`, deployed to the Edit Scripts folder by
`SSEEdit_Scripts/deploy.sh`). Expect `ПРОБЛЕМ: 0`. This rewrites:

- `RFAB_SurvivalLayer.esp`
- `scripts/source/_RSL_Forms.psc` — **tracked** (generated code), commit it
- `scripts/source/_RSL_Balance.psc` — **tracked**, commit it
- `MCM/Config/RFAB_SurvivalLayer/config.json` — **tracked**, commit it

Move the regenerated `.esp` out of MO2's Overwrite into the mod folder.

## 2. Compile Papyrus

```
_build.bat
```

Produces `scripts/*.pex` from `scripts/source/*.psc`.

## 3. Build the HUD widget (only if `widget/*.as` or `widget/icons/*.png` changed)

```
widget/build_widget.sh
```

Produces `Interface/widgets/RFABSurvivalLayer/RSLHud.swf` (and the
`Interface/exported/` copy). Needs FFDec + Python (Pillow). Inputs:
`widget/_base_skyui_activeeffects.swf` (SkyUI SDK), `widget/src/**/*.as`,
`widget/icons/*.png` (embedded as `ico_*` by `widget/embed_icons.py`).

## 4. Sanity pass

- `SSEEdit_Scripts/RFAB_Validate_Deps.pas` (Apply Script) → all PASS.
- Load a save, smoke-test sleep / eat / cold / a disease / hypothermia.

## 5. Package and publish

The release archive is the mod folder as MO2 installs it — everything except
`.git/`, `SSEEdit_Scripts/`, `widget/`, `tools/`, `docs/`, and the `*.md`
files. Zip it, then:

```
gh release create vX.Y --title "vX.Y" --notes "..." \
  RFAB_SurvivalLayer.zip
```

Or attach the loose `.esp` + `Interface/` + `scripts/` + `MCM/` +
`Interface/Translations/` if the archive is assembled elsewhere.

Tested against **RFAB SE XI - Respect Edition [ver. 09.07.2026]** — note the
tested-on version in the release notes.
