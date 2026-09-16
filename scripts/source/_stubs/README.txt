HEADER STUBS FOR THE PAPYRUS COMPILER
====================================

These .psc files are NOT part of the mod. They declare vanilla classes that
are missing from the pack because Creation Kit is not installed. The compiler
only needs the signatures - every implementation lives in the game engine.

RULE: NEVER compile this folder.
A compiled GlobalVariable.pex would override the vanilla one and break the
game. The folder is only ever passed to the compiler via -i (import path).

The Creation Kit turned out to be installed after all, and tools\compile_mcm.bat
now takes the real vanilla sources from its Data\Scripts.zip - unpacked
anywhere, pointed at by CK_SOURCE. They come FIRST on the import path, so these
stubs no longer shadow anything.

What still has to live here is MCM_ConfigBase: it ships compiled with MCM Helper
and has no source at all. The rest are vestigial and can go whenever someone
checks that the compile still passes without them.

Contents checked against:
  - real SkyUI code (SKI_PlayerLoadGameAlias.psc) - OnPlayerLoadGame()
  - Alias.psc from SKSE - base class, complete, no stub needed
