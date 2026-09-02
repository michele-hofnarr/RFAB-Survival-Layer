@echo off
REM RFAB Survival Layer - Papyrus build. No Creation Kit needed: compiler comes
REM from the Nemesis package, missing vanilla declarations from scripts\source\_stubs.
REM Usage:  _build.bat _RSL_Controller   (one script)   |   _build.bat   (all)
REM See README.md.

set GAME=R:\Games\The Elder Scrolls V Skyrim - Special Edition
set MOD=%GAME%\MO2\mods\RFAB Survival Layer
set PC=%GAME%\MO2\mods\Nemesis Unlimited Behavior Engine SE\Nemesis_Engine\Papyrus Compiler

REM Import path order matters:
REM   1. Data\Scripts\Source - SKSE versions must win
REM   2. _stubs              - must precede "backup scripts" (else the stripped
REM                            Nemesis Debug.psc wins, no TraceStack)
REM   3. backup scripts      - Actor/Form/ObjectReference/Debug from Nemesis
set IMPORTS=%GAME%\Data\Scripts\Source
set IMPORTS=%IMPORTS%;%MOD%\scripts\source\_stubs
set IMPORTS=%IMPORTS%;%PC%\backup scripts
set IMPORTS=%IMPORTS%;%GAME%\MO2\mods\[RFAB] Papyrus Extenders\scripts\source
set IMPORTS=%IMPORTS%;%GAME%\MO2\mods\SkyUI_5_2_SE\scripts\source
set IMPORTS=%IMPORTS%;%MOD%\scripts\source

if "%~1"=="" goto buildall
"%PC%\PapyrusCompiler.exe" "%~1" -f="%PC%\scripts\TESV_Papyrus_Flags.flg" -i="%IMPORTS%" -o="%MOD%\scripts"
goto done

:buildall
REM -all is not recursive, so _stubs is skipped. Never compile _stubs: a compiled
REM GlobalVariable.pex would override the vanilla one and break the game.
"%PC%\PapyrusCompiler.exe" "%MOD%\scripts\source" -f="%PC%\scripts\TESV_Papyrus_Flags.flg" -i="%IMPORTS%" -o="%MOD%\scripts" -all

:done
echo.
pause
