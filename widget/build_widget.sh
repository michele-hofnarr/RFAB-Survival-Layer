#!/bin/sh
# Builds the RFAB Survival Layer HUD widget .swf.
#
# SkyUI (SKI_WidgetBase) route: a standalone .swf that SkyUI loads into the HUD
# and positions, fed values from Papyrus via UI.Invoke*. No Flash editor: take a
# shipped SkyUI widget as the shell (SDK classes WidgetBase/GlobalFunc/Tween/
# Delegate already compiled in), rename its namespace so _global does not clash
# with SkyUI's own active-effects widget, then swap the impl class via
# FFDec -importScript (which can only replace existing scripts, not add).
#
# Deps: java, FFDec (JPEXS) at the path below.
# In:   _base_skyui_activeeffects.swf  (copy of SkyUI activeeffects.swf)
#       src/__Packages/skyui/widgets/rfab_survival/*.as
# Out:  ../Interface/exported/widgets/RFABSurvivalLayer/RSLHud.swf (+ fallback path)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FF="R:/Games/The Elder Scrolls V Skyrim - Special Edition/develop/ffdec_26.2.1/ffdec.jar"
BASE="$HERE/_base_skyui_activeeffects.swf"
OUT="$HERE/../Interface/exported/widgets/RFABSurvivalLayer/RSLHud.swf"
OUT2="$HERE/../Interface/widgets/RFABSurvivalLayer/RSLHud.swf"
TMP="$HERE/.build"

rm -rf "$TMP"; mkdir -p "$TMP"
java -jar "$FF" -decompress "$BASE" "$TMP/raw.swf"

# Byte-level namespace rename (same-length strings keep offsets intact).
python3 - "$TMP/raw.swf" "$TMP/renamed.swf" <<'PY'
import sys
src,dst=sys.argv[1],sys.argv[2]
d=bytearray(open(src,'rb').read())
pairs=[(b'ActiveEffectsWidget',b'Rfab_SurvivalWidget'),
       (b'ActiveEffectsGroup', b'Rfab_SurvivalGroup'),
       (b'ActiveEffect',       b'Rfab_Surviv1'),
       (b'activeeffects',      b'rfab_survival')]
for a,b in pairs:
    assert len(a)==len(b), (a,b)
    d[:]=d.replace(a,b)
open(dst,'wb').write(d)
PY

java -jar "$FF" -importScript "$TMP/renamed.swf" "$TMP/built.swf" "$HERE/src"

# ZLIB (CWS) pack: FFDec -compress is unreliable here, do it ourselves.
mkdir -p "$(dirname "$OUT")"
python3 - "$TMP/built.swf" "$OUT" <<'PY2'
import sys,zlib
raw=open(sys.argv[1],'rb').read()
if raw[:3] in (b'CWS',b'ZWS'):
    open(sys.argv[2],'wb').write(raw)
else:
    body=zlib.compress(raw[8:],9)
    open(sys.argv[2],'wb').write(b'CWS'+raw[3:4]+raw[4:8]+body)
PY2
mkdir -p "$(dirname "$OUT2")"; cp "$OUT" "$OUT2"
rm -rf "$TMP"
echo "OK -> $OUT (+ fallback $OUT2)"
