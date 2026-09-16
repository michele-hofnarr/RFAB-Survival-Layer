@echo off
REM Compiles scripts\source\_RSL_MCM.psc to scripts\_RSL_MCM.pex.
REM
REM Run it after tools\make_mcm.py, which GENERATES that .psc: the reset button
REM calls ResetDefaults(), and ResetDefaults() is one SetModSetting per setting,
REM written from the same table as settings.ini. Add a setting and skip this
REM step, and the button silently stops resetting that one.
REM
REM Three source trees are needed and none of them ship with the mod:
REM   _stubs   - our own headers for classes the packs are missing
REM   SkyUI    - SKI_ConfigBase, which MCM_ConfigBase extends
REM   vanilla  - Quest, Form and the rest of the chain, from the Creation Kit's
REM              Data\Scripts.zip. Unpack it anywhere and point CK_SOURCE at it;
REM              the game install is not modified.
setlocal
set "MOD=%~dp0.."
set "PC=R:\SteamLibrary\steamapps\common\Skyrim Special Edition\Papyrus Compiler"
set "SKYUI=R:\Games\The Elder Scrolls V Skyrim - Special Edition\MO2\mods\SkyUI_5_2_SE\scripts\source"
if "%CK_SOURCE%"=="" set "CK_SOURCE=C:\Users\Fredd\AppData\Local\Temp\claude\ck_scripts\Source\Scripts"

if not exist "%PC%\PapyrusCompiler.exe" (
  echo Papyrus compiler not found: "%PC%"
  exit /b 1
)
if not exist "%CK_SOURCE%\Quest.psc" (
  echo Vanilla script sources not found: "%CK_SOURCE%"
  echo Unpack "Data\Scripts.zip" from the Creation Kit and set CK_SOURCE.
  exit /b 1
)

"%PC%\PapyrusCompiler.exe" "%MOD%\scripts\source\_RSL_MCM.psc" ^
  -f="%CK_SOURCE%\TESV_Papyrus_Flags.flg" ^
  -i="%MOD%\scripts\source;%SKYUI%;%CK_SOURCE%;%MOD%\scripts\source\_stubs" ^
  -o="%MOD%\scripts"
