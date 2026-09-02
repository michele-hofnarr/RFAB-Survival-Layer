#!/bin/sh
# Copies the generator scripts into SSEEdit's Edit Scripts folder, recoding
# UTF-8 -> CP1251. The xEdit script engine reads .pas (and the data files it
# loads with LoadFromFile) as single-byte CP1251; a UTF-8 file compiles but
# every Russian line comes out as mojibake. Repo copies stay UTF-8.
#
# Without //IGNORE, iconv silently TRUNCATES the file at the first non-CP1251
# char (x . -> etc), which xEdit then reports as "'end' expected but EOF found".
# So: strict iconv + a line-count check that nothing was lost.

SRC="R:/Games/The Elder Scrolls V Skyrim - Special Edition/MO2/mods/RFAB Survival Layer/SSEEdit_Scripts"
DST="R:/torrent/SSEEdit 4.1.5f-164-4-1-5f-1714283656/SSEEdit 4.1.5f/Edit Scripts"

fail=0

# recode one file UTF-8 -> CP1251, aborting if any char is lost
recode() {
    f="$1"
    name=$(basename "$f")
    src_lines=$(grep -c '' "$f")
    if ! iconv -f UTF-8 -t CP1251 < "$f" > "$DST/$name" 2>/dev/null; then
        echo "  !! $name -- iconv FAILED (non-CP1251 char). NOT copied."
        fail=1
        return
    fi
    dst_lines=$(grep -c '' "$DST/$name")
    if [ "$src_lines" != "$dst_lines" ]; then
        echo "  !! $name -- lines lost in recode ($src_lines -> $dst_lines)."
        fail=1
        return
    fi
    echo "  $name -> CP1251 ($dst_lines lines)"
}

for f in "$SRC"/*.pas; do
    recode "$f"
done
recode "$SRC/RFAB_SurvivalLayer_strings.txt"

if [ "$fail" != 0 ]; then
    echo "ERROR: some files not recoded. Remove non-CP1251 chars from the source."
    exit 1
fi
echo "done"
