#!/usr/bin/env python3
"""Build the MCM translation files from the single Russian source.

All user-facing Russian text lives in one UTF-8 file,
SSEEdit_Scripts/RFAB_SurvivalLayer_strings.txt. Keys prefixed "mcm." are the
MCM Helper strings; this script turns them into the two files SkyUI actually
reads, which are UTF-16LE with a BOM and TAB-separated:

    Interface/Translations/RFAB_SurvivalLayer_russian.txt
    Interface/Translations/RFAB_SurvivalLayer_english.txt

Both get the same Russian text - the mod is Russian-only, and the English file
exists so the strings still resolve when the game runs in English. Generating
both from one source also keeps them from drifting apart, which they had.

The generator (.pas) is not used for this: xEdit's TStringList cannot write
UTF-16, and the .pas already reads the same source file for the ESP records.

Usage:
    python3 tools/build_translations.py            # build both files
    python3 tools/build_translations.py --check    # verify they are up to date
    python3 tools/build_translations.py --migrate  # one-off: dump the CURRENT
                                                   # russian.txt as mcm.* lines
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "SSEEdit_Scripts" / "RFAB_SurvivalLayer_strings.txt"
OUT_DIR = ROOT / "Interface" / "Translations"
OUT = [OUT_DIR / "RFAB_SurvivalLayer_russian.txt",
       OUT_DIR / "RFAB_SurvivalLayer_english.txt"]

PREFIX = "mcm."

# deploy.sh recodes the source file to CP1251 for xEdit and aborts on any
# character that does not fit. The old MCM texts used a few that do not:
# replace them on the way in so the single source stays CP1251-clean.
NOT_IN_CP1251 = {"×": "x", "−": "-"}


def read_source():
    """Ordered (key, value) for every mcm.* entry in the source file."""
    pairs = []
    seen = set()
    for n, line in enumerate(SRC.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        if not key.startswith(PREFIX):
            continue
        key = key[len(PREFIX):]
        if not key.startswith("$"):
            key = "$" + key
        if key in seen:
            raise SystemExit(f"{SRC.name}:{n}: duplicate key {key}")
        seen.add(key)
        pairs.append((key, value))
    return pairs


def render(pairs):
    """The exact bytes SkyUI expects: BOM, UTF-16LE, "$KEY<TAB>value\r\n"."""
    body = "".join(f"{k}\t{v}\r\n" for k, v in pairs)
    return b"\xff\xfe" + body.encode("utf-16-le")


def migrate():
    """Print the current russian.txt back as mcm.* lines for strings.txt.

    Run once when moving the strings into the single source; the output is
    meant to be pasted (or appended) into RFAB_SurvivalLayer_strings.txt.
    """
    text = OUT[0].read_bytes().decode("utf-16-le").lstrip("﻿")
    for bad, good in NOT_IN_CP1251.items():
        text = text.replace(bad, good)
    out = []
    for line in text.splitlines():
        line = line.rstrip("\r")
        if not line.strip():
            continue
        key, tab, value = line.partition("\t")
        if not tab:
            raise SystemExit(f"no TAB in line: {line!r}")
        out.append(f"{PREFIX}{key.strip()}={value}")
    # Straight to the byte stream: the Windows console is CP1251 here and would
    # otherwise refuse the Russian text outright.
    sys.stdout.buffer.write(("\n".join(out) + "\n").encode("utf-8"))


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "--migrate":
        migrate()
        return 0

    pairs = read_source()
    if not pairs:
        raise SystemExit(f"no {PREFIX}* keys in {SRC} - nothing to build")
    data = render(pairs)

    if arg == "--check":
        stale = [p.name for p in OUT if not p.exists() or p.read_bytes() != data]
        if stale:
            print("out of date: " + ", ".join(stale))
            return 1
        print(f"up to date ({len(pairs)} strings)")
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for p in OUT:
        p.write_bytes(data)
        print(f"  {p.relative_to(ROOT)} <- {len(pairs)} strings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
