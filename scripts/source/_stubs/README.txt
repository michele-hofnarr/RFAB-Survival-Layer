HEADER STUBS FOR THE PAPYRUS COMPILER
====================================

These .psc files are NOT part of the mod. They declare vanilla classes that
are missing from the pack because Creation Kit is not installed. The compiler
only needs the signatures - every implementation lives in the game engine.

RULE: NEVER compile this folder.
A compiled GlobalVariable.pex would override the vanilla one and break the
game. The folder is only ever passed to the compiler via -i (import path).

If a real Creation Kit becomes available, delete this folder entirely and
point the build script at <CK>\Data\Scripts\Source.

Contents checked against:
  - real SkyUI code (SKI_PlayerLoadGameAlias.psc) - OnPlayerLoadGame()
  - Alias.psc from SKSE - base class, complete, no stub needed
