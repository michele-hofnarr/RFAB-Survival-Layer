{
  RFAB Survival Layer - plugin generator, part 1: records.

  Creates in RFAB_SurvivalLayer.esp: ~60 GLOB (settings, also read by MCM
  Helper), 1 FLST (fire sources), 12 MGEF (Value Modifier on H/M/S/SpeedMult -
  axis penalties), 3 SPEL (axis abilities), 1 Script-MGEF + 1 SPEL (monitor,
  carries all logic), 3 QUST (Start Game Enabled: controller, MCM, widget),
  and the VMAD bindings. Also emits two derived files so no formID is ever
  hand-copied: scripts\source\_RSL_Forms.psc and
  MCM\Config\RFAB_SurvivalLayer\config.json.

  Run: SSEEdit via MO2 with -D:"...\Data\", right-click Skyrim.esm -> Apply
  Script. Idempotent: reruns do not duplicate.

  MGEF/SPEL are not built from scratch - a vanilla template is located in
  Skyrim.esm, its archetype verified, then copied. Every unresolved path is
  logged with the word PROBLEM; any such line means the plugin is incomplete.
  API signatures are checked against Edit Scripts\xEditAPI.pas (SSEEdit 4.1.5f).
}
unit RFAB_SurvivalLayer_01_Records;

const
  PLUGIN_NAME = 'RFAB_SurvivalLayer.esp';
  PFX         = '_RSL_';
  MOD_DIR     = 'R:\Games\The Elder Scrolls V Skyrim - Special Edition\MO2\mods\RFAB Survival Layer\';

  // Detrimental makes the game show the effect as harmful (red). Paired with
  // Recover (copied from the template) it still modifies the max pool, not the
  // current value. Set False + regenerate if the max stops dropping in-game.
  MARK_DETRIMENTAL = True;

  FLAG_DETRIMENTAL = $00000004;

  // Penalty defaults (percent, multiples of 5). Must match BuildGlobals;
  // used to fill explicit numbers into effect descriptions.
  DEF_PENALTY_PRIMARY = 60;   // axis's primary pool at full bar
  DEF_PENALTY_CROSS   = 10;   // the two other pools
  DEF_PENALTY_SPEED   = 10;   // SpeedMult, per axis

var
  tgt      : IwbFile;
  problems : Integer;
  madeNew  : Integer;
  reused   : Integer;
  ids      : TStringList;   // EDID = local formID (hex)
  cureForms: TStringList;   // "hex6=filename" per cure-disease MGEF
  balDefaults: TStringList; // "EDID=default" per GLOB - emitted as _RSL_Balance.psc
  strTbl   : TStringList;   // key=value, all user-facing RU text (strings.txt)

// --- helpers ---------------------------------------------------------------

procedure Say(s: string);
begin
  AddMessage(s);
end;

procedure Problem(s: string);
begin
  Inc(problems);
  AddMessage('  ПРОБЛЕМА: ' + s);
end;

// Load all user-facing RU text from RFAB_SurvivalLayer_strings.txt (sits next
// to this script; deploy.sh recodes it to CP1251). key=value, # = comment.
procedure LoadStrings;
var
  raw: TStringList;
  i, p: Integer;
  ln: string;
begin
  strTbl := TStringList.Create;
  raw := TStringList.Create;
  try
    raw.LoadFromFile(ScriptsPath + 'RFAB_SurvivalLayer_strings.txt');
    for i := 0 to Pred(raw.Count) do begin
      ln := raw[i];
      if (ln = '') or (Copy(ln, 1, 1) = '#') then Continue;
      p := Pos('=', ln);
      if p = 0 then Continue;
      strTbl.Values[Trim(Copy(ln, 1, p - 1))] := Copy(ln, p + 1, Length(ln));
    end;
  finally
    raw.Free;
  end;
  Say('  strings: ' + IntToStr(strTbl.Count) + ' entries');
end;

// Localised string by key. Missing key = a loud PROBLEM + the key itself.
function L(key: string): string;
begin
  if strTbl.IndexOfName(key) < 0 then begin
    Problem('missing string key: ' + key);
    Result := key;
    Exit;
  end;
  Result := strTbl.Values[key];
end;

// Expand %P / %C / %S in a description to the penalty defaults, angle-bracketed
// (the RFAB metric highlight). Used for the axis-ability descriptions.
function ExpandP(s: string): string;
var
  i, n: Integer;
  c, sub: string;
begin
  Result := '';
  i := 1;
  n := Length(s);
  while i <= n do begin
    sub := '';
    if (s[i] = '%') and (i < n) then begin
      c := s[i + 1];
      if      c = 'P' then sub := '<' + IntToStr(DEF_PENALTY_PRIMARY) + '>'
      else if c = 'C' then sub := '<' + IntToStr(DEF_PENALTY_CROSS) + '>'
      else if c = 'S' then sub := '<' + IntToStr(DEF_PENALTY_SPEED) + '>';
    end;
    if sub <> '' then begin
      Result := Result + sub;
      i := i + 2;
    end else begin
      Result := Result + s[i];
      i := i + 1;
    end;
  end;
end;

// Sets a value by path and reports failure (bare SetElementEditValues is
// silent). Tries add-then-set: a freshly created record has no subrecords yet.
function PutEdit(rec: IInterface; path: string; value: string): Boolean;
var
  el: IInterface;
begin
  el := ElementByPath(rec, path);

  if not Assigned(el) then
    el := Add(rec, path, True);

  if Assigned(el) then begin
    // SetEditValue on an enum field (e.g. Actor Value) with an unknown string
    // raises EConvertError and kills the whole script - flag and continue.
    try
      SetEditValue(el, value);
      Result := True;
      Exit;
    except
      on E: Exception do begin
        Problem('SetEditValue "' + value + '" at "' + path + '" in '
              + Name(rec) + ': ' + E.Message);
        Result := False;
        Exit;
      end;
    end;
  end;

  // Last try: this one can also create nested paths.
  SetElementEditValues(rec, path, value);
  if GetElementEditValues(rec, path) = value then begin
    Result := True;
    Exit;
  end;

  Problem('не удалось задать "' + path + '" в ' + Name(rec));
  Result := False;
end;

function PutNative(rec: IInterface; path: string; value: Variant): Boolean;
var
  el: IInterface;
begin
  el := ElementByPath(rec, path);

  if not Assigned(el) then
    el := Add(rec, path, True);

  if Assigned(el) then begin
    SetNativeValue(el, value);
    Result := True;
    Exit;
  end;

  SetElementNativeValues(rec, path, value);
  if Assigned(ElementByPath(rec, path)) then begin
    Result := True;
    Exit;
  end;

  Problem('не удалось задать "' + path + '" в ' + Name(rec));
  Result := False;
end;

// Local formID: high byte (load-order index) zeroed - the form
// Game.GetFormFromFile expects.
function LocalID(rec: IwbMainRecord): Cardinal;
begin
  Result := GetLoadOrderFormID(rec) and $00FFFFFF;
end;

function LocalIDHex(rec: IwbMainRecord): string;
begin
  Result := IntToHex(LocalID(rec), 6);
end;

// Store a formID by name so the file emitters can find it.
procedure Remember(edid: string; rec: IwbMainRecord);
begin
  if Assigned(rec) then
    ids.Values[edid] := LocalIDHex(rec);
end;

function RecalledHex(edid: string): string;
begin
  Result := ids.Values[edid];
  if Result = '' then begin
    Problem('не запомнен formID для ' + edid);
    Result := '000000';
  end;
end;

// For MCM Helper: "plugin|8xx", no leading zeros.
function SourceForm(edid: string): string;
var
  h: string;
begin
  h := RecalledHex(edid);
  while (Length(h) > 1) and (h[1] = '0') do
    Delete(h, 1, 1);
  Result := PLUGIN_NAME + '|' + h;
end;

// --- files and records ---------------------------------------------------

function FileByName(fname: string): IwbFile;
var
  i: Integer;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do
    if SameText(GetFileName(FileByIndex(i)), fname) then begin
      Result := FileByIndex(i);
      Exit;
    end;
end;

function EnsureTargetFile: IwbFile;
begin
  Result := FileByName(PLUGIN_NAME);
  if Assigned(Result) then begin
    Say('Целевой плагин уже загружен: ' + PLUGIN_NAME);
    Exit;
  end;

  // In SSEEdit 4.1.5f this takes exactly one argument.
  Result := AddNewFileName(PLUGIN_NAME);

  if not Assigned(Result) then begin
    Say('');
    Say('НЕ УДАЛОСЬ СОЗДАТЬ ' + PLUGIN_NAME + ' автоматически.');
    Say('Создайте вручную (ПКМ в левой панели -> Add) и запустите снова.');
    Exit;
  end;

  AddMasterIfMissing(Result, 'Skyrim.esm');
  Say('Создан ' + PLUGIN_NAME + ' (мастер: Skyrim.esm)');
end;

function RecordByEDID(f: IwbFile; sig: string; edid: string): IwbMainRecord;
var
  grp: IwbGroupRecord;
  i  : Integer;
  r  : IwbMainRecord;
begin
  Result := nil;
  grp := GroupBySignature(f, sig);
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if SameText(EditorID(r), edid) then begin
      Result := r;
      Exit;
    end;
  end;
end;

function EnsureGroup(sig: string): IwbGroupRecord;
begin
  Result := GroupBySignature(tgt, sig);
  if not Assigned(Result) then
    Result := Add(tgt, sig, True);
  if not Assigned(Result) then
    Problem('не удалось создать группу ' + sig);
end;

// --- GLOB: settings -----------------------------------------------------

function AddGlobal(edid: string; value: Real; kind: string): IwbMainRecord;
var
  grp: IwbGroupRecord;
begin
  // record the default so WriteBalanceScript can emit _RSL_Balance.ResetDefaults.
  // All GLOB defaults are whole numbers; add float formatting if that changes.
  balDefaults.Add(edid + '=' + IntToStr(Round(value)));

  Result := RecordByEDID(tgt, 'GLOB', edid);
  if Assigned(Result) then begin
    // Refresh the value on reuse too: plugin FLTV is the default for a NEW
    // game. A running save keeps its own GLOB values (MCM writes them);
    // regen does not touch those. Keeps the plugin at current balance.
    PutEdit(Result, 'FNAM', kind);
    PutNative(Result, 'FLTV', value);
    Inc(reused);
    Remember(edid, Result);
    Exit;
  end;

  grp := EnsureGroup('GLOB');
  if not Assigned(grp) then Exit;

  Result := Add(grp, 'GLOB', True);
  if not Assigned(Result) then begin
    Problem('не создался GLOB ' + edid);
    Exit;
  end;

  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'FNAM', kind);
  PutNative(Result, 'FLTV', value);
  Remember(edid, Result);
  Inc(madeNew);
end;

procedure BuildGlobals;
begin
  Say('');
  Say('--- GLOB: settings ---');

  // master switch
  AddGlobal(PFX + 'ModEnabled',          1,     'Short');

  // SLEEP axis
  AddGlobal(PFX + 'SleepGrace',          16,    'Float');
  AddGlobal(PFX + 'SleepMax',            48,    'Float');
  AddGlobal(PFX + 'SleepRestorePerHour', 6,     'Float');
  AddGlobal(PFX + 'SleepMinHours',       1,     'Float');
  // Combat multiplies the sleep AND hunger accrual rate (GetCombatState == 1).
  AddGlobal(PFX + 'CombatFatigueMult',   5,     'Float');

  // HUNGER axis. Food restore is weight-scaled: Food*Pct = % of the bar per kg
  // of item weight (DATA - Weight). Plain Food -> HungerFoodPct; RFAB_SpecialFood
  // or RFAB_RawFood (with a strong stomach) -> HungerSpecialFoodPct.
  AddGlobal(PFX + 'HungerGrace',         12,    'Float');
  AddGlobal(PFX + 'HungerMax',           48,    'Float');
  AddGlobal(PFX + 'HungerFoodPct',        50,    'Float');   // % of bar per kg, plain Food
  AddGlobal(PFX + 'HungerSpecialFoodPct', 100,   'Float');   // % of bar per kg, meal / raw-if-hardy

  // COLD model (multiplicative):
  //   sev = RegionBase x Weather x Night x Swim x Fire (RegionBase rises with altitude)
  //   delta/game-hour = (sev - Mitigation) x ColdRate
  //   Mitigation = slots x WarmthPerSlot x Wetness + FrostResist x ResistWeight/100
  // Multipliers stored as percent (multiples of 5), script divides by 100.
  // Timings tuned for TimeScale=10: naked, 0 resist, cold 0->100:
  //   cold region day/clear 15 real min - mid 30 - warm 60 (x2 swim/blizzard).
  // Dressed (4 slots) + Acclimatization perk (25 resist): cold day/clear gap 0.

  // flat region base (replaces the old HoldMult multiplier)
  AddGlobal(PFX + 'RegionWinterhold',    40,    'Float');
  AddGlobal(PFX + 'RegionPale',          40,    'Float');
  AddGlobal(PFX + 'RegionEastmarch',     40,    'Float');
  AddGlobal(PFX + 'RegionReach',         20,    'Float');
  AddGlobal(PFX + 'RegionHjaalmarch',    20,    'Float');
  AddGlobal(PFX + 'RegionHaafingar',     20,    'Float');
  AddGlobal(PFX + 'RegionWhiterun',      10,    'Float');
  AddGlobal(PFX + 'RegionFalkreath',     10,    'Float');
  AddGlobal(PFX + 'RegionRift',          10,    'Float');
  AddGlobal(PFX + 'RegionDefault',       20,    'Float');   // unknown hold
  // If the current region CAN snow (Weather.FindWeather(3) != None - the same
  // check RFAB's Control Weather spell uses), it is a cold climate even under
  // a clear sky. Floor the hold base at this. A warm hold's high spots
  // (Ветреный Пик etc.) sit in a snow region the altitude Z ramp misses.
  AddGlobal(PFX + 'RegionSnowFloor',     40,    'Float');
  // Mountain severity floor. Between AltitudeLow and AltitudeHigh the base
  // interpolates hold_base -> RegionAltitude by Z height.
  AddGlobal(PFX + 'RegionAltitude',      100,   'Float');

  // multipliers (percent), 100 = x1.0
  AddGlobal(PFX + 'WeatherClear',        100,   'Float');
  AddGlobal(PFX + 'WeatherCloudy',       150,   'Float');
  AddGlobal(PFX + 'WeatherRain',         200,   'Float');
  AddGlobal(PFX + 'WeatherSnow',         200,   'Float');
  AddGlobal(PFX + 'NightMult',           170,   'Float');   // 22:00-06:00
  AddGlobal(PFX + 'SwimMult',            220,   'Float');   // + clothing warmth off
  AddGlobal(PFX + 'FireMult',            40,    'Float');   // outdoor fire/torch (x0.4); interior+fire -> 0

  AddGlobal(PFX + 'SevInterior',         25,    'Float');   // interior base, no fire
  AddGlobal(PFX + 'SevColdInterior',     45,    'Float');   // ice cave / frozen ruin: fire only partly warms (x FireMult)
  AddGlobal(PFX + 'AltitudeLow',         8000,  'Float');   // below -> hold base
  AddGlobal(PFX + 'AltitudeHigh',        14000, 'Float');   // above -> full RegionAltitude
  AddGlobal(PFX + 'FireRadius',          400,   'Float');

  AddGlobal(PFX + 'ColdRate',            1.0,   'Float');   // overall rate multiplier
  AddGlobal(PFX + 'ColdGrace',           25,    'Float');   // stat penalty ramps cold 25..100; widget notch at 75%
  AddGlobal(PFX + 'WarmupMult',          5.0,   'Float');   // warm-up N x faster than cooling
  AddGlobal(PFX + 'WarmthPerSlot',       7,     'Float');   // warmth per clothing slot (4 slots = 28)
  AddGlobal(PFX + 'ResistWeight',        50,    'Float');   // FrostResist x 50%
  AddGlobal(PFX + 'DryMinutes',          5,     'Float');
  // Per-hit nudge on the cold bar (MagicDamageFrost / MagicDamageFire effects),
  // rate-limited to one hit per 0.5 s. 0 disables. Fire warming is deliberately
  // exploitable - burning costs HP.
  AddGlobal(PFX + 'FrostHitCold',        2,     'Float');
  AddGlobal(PFX + 'FireHitWarm',         2,     'Float');

  // penalties, all multiples of 5
  AddGlobal(PFX + 'PenaltyPrimary',      60,    'Float');
  AddGlobal(PFX + 'PenaltyCross',        10,    'Float');
  AddGlobal(PFX + 'PenaltySpeed',        10,    'Float');   // SpeedMult, per axis
  AddGlobal(PFX + 'SpeedCap',            30,    'Float');   // total cap; RFAB burden already takes up to 50 SpeedMult
  AddGlobal(PFX + 'PenaltyCap',          85,    'Float');
  AddGlobal(PFX + 'TierStep',            5,     'Float');

  // cold visual (character ice shader only; screen ISM dropped - no vanilla
  // IMAD holds visually)
  AddGlobal(PFX + 'ColdVisualShader',    1,     'Short');   // ice crust on character (on)
  AddGlobal(PFX + 'ColdVisualThreshold', 90,    'Float');   // cold >= 90 -> ice crust

  // diseases. Progress = worsen, Decay = improve; 24 game-hours each for now.
  AddGlobal(PFX + 'DiseaseEnabled',      1,     'Short');
  AddGlobal(PFX + 'DiseaseProgressHours', 24,   'Float');
  AddGlobal(PFX + 'DiseaseDecayHours',   24,    'Float');
  // wrappers over RFAB's own 6 diseases + Droops: progressive stages 2/3
  AddGlobal(PFX + 'RfabDzEnabled',       1,     'Short');
  // % chance to catch an OnHit disease per melee hit from a carrier
  // (draugr/troll/slaughterfish), then * (1 - DiseaseResist/100).
  AddGlobal(PFX + 'DiseaseHitChance',    100,   'Float');
  // % chance to get food poisoning per raw-food item eaten. Flat - no
  // DiseaseResist; only the strong-stomach races / already-sick are immune.
  AddGlobal(PFX + 'FoodPoisonChance',    50,    'Float');
  // hypothermia (an Ability, not a disease): P 0..100 threshold-crossing.
  // cold>=Threshold -> P +100 over WorsenHours -> stage+1, P=0.
  // cold<=RecoverThr -> P -100 over RecoverHours -> stage-1. else frozen.
  AddGlobal(PFX + 'HypothermiaEnabled',      1,  'Short');
  AddGlobal(PFX + 'HypothermiaThreshold',    90, 'Float');
  AddGlobal(PFX + 'HypothermiaRecoverThr',   25, 'Float');
  AddGlobal(PFX + 'HypothermiaWorsenHours',  1,  'Float');
  AddGlobal(PFX + 'HypothermiaRecoverHours', 1,  'Float');
  AddGlobal(PFX + 'HypothermiaDrainPerSec',  1,  'Float');   // stage 3 HP/sec base
  AddGlobal(PFX + 'HypothermiaDrainRamp',    30, 'Float');   // sec for the drain to ~double
  // common-cold contract chance/game-hour, linear from Min at Threshold to
  // Max at MaxAt cold level, then * (1 - DiseaseResist/100).
  AddGlobal(PFX + 'ColdColdThreshold',        50, 'Float');
  AddGlobal(PFX + 'ColdColdChanceMin',   10,    'Float');
  AddGlobal(PFX + 'ColdColdChanceMax',   90,    'Float');
  AddGlobal(PFX + 'ColdColdChanceMaxAt', 90,    'Float');
  // elemental lesions (frostbite/burns): a 3-stage Disease-type SPEL with a
  // bespoke P model - worsens from cold>=ColdThr and from frost/fire/shock hits
  // (P -= HitP x resist), heals only when every axis is clear, +BandageP per
  // RFAB_Bandage used. Contract: P<=-ContractP, or a roll at hypothermia st.>=2.
  AddGlobal(PFX + 'ElemLesionEnabled',       1,  'Short');
  AddGlobal(PFX + 'ElemLesionColdThr',       90, 'Float');   // cold >= this worsens existing lesions
  AddGlobal(PFX + 'ElemLesionHypoChance',    50, 'Float');   // %/game-hour to contract at hypothermia stage >= 2
  AddGlobal(PFX + 'ElemLesionHitP',          4,  'Float');   // P per elemental hit (pre-resist)
  AddGlobal(PFX + 'ElemLesionContractP',     70, 'Float');   // |P| to contract from hits
  AddGlobal(PFX + 'ElemLesionBandageP',      10, 'Float');

  // HUD widget. HudWidget = master on/off; the 3 bars always show together.
  AddGlobal(PFX + 'HudWidget',           1,     'Short');
  AddGlobal(PFX + 'HudColor',            1,     'Short');   // 1 = tinted icons/bars, 0 = plain white
  AddGlobal(PFX + 'HudWidgetAutoHide',   0,     'Short');   // hide when all safe (off by default)
  AddGlobal(PFX + 'HudWidgetX',          220,   'Float');    // px at 1280 wide
  AddGlobal(PFX + 'HudWidgetY',          655,   'Float');    // px at 720 tall
  AddGlobal(PFX + 'HudWidgetScale',      100,   'Float');    // %
  AddGlobal(PFX + 'HudWidgetAlpha',      100,   'Float');    // %
  AddGlobal(PFX + 'HudWidgetHAnchor',    0,     'Short');    // 0 left 1 center 2 right
  AddGlobal(PFX + 'HudWidgetVAnchor',    0,     'Short');    // 0 top 1 center 2 bottom

  // service
  AddGlobal(PFX + 'PollInterval',        1.0,   'Float');
  AddGlobal(PFX + 'DebugLog',            0,     'Short');   // gates all RSL_debug.log writes
end;

// --- FLST: fire sources -------------------------------------------------

// Mask kept deliberately narrow: under-matching and adding by hand beats
// cleaning out junk.
function LooksLikeFire(edid: string): Boolean;
var
  s: string;
begin
  s := LowerCase(edid);
  // drop clearly cold / snuffed / decorative / helper records
  if (Pos('unlit',    s) > 0) or (Pos('snuffed',  s) > 0)
  or (Pos('01off',    s) > 0) or (Pos('landoff',  s) > 0)
  or (Pos('embersout',s) > 0) or (Pos('cold',     s) > 0)
  or (Pos('nofire',   s) > 0) or (Pos('_ash',     s) > 0)
  or (Pos('fence',    s) > 0) or (Pos('lod',      s) > 0)
  or (Pos('decal',    s) > 0) or (Pos('deco0',    s) > 0)
  or (Pos('steamoff', s) > 0) or (Pos('pipeoff',  s) > 0)
  or (Pos('sign',     s) > 0) or (Pos('freeflowrock', s) > 0)
  or (Pos('test',     s) > 0) or (Pos('magdragon',s) > 0) then begin
    Result := False;
    Exit;
  end;
  Result :=
    (Pos('campfire',    s) > 0) or
    (Pos('firepit',     s) > 0) or
    (Pos('firespit',    s) > 0) or
    (Pos('brazier',     s) > 0) or
    (Pos('firebowl',    s) > 0) or
    (Pos('bonfire',     s) > 0) or
    (Pos('fireplace',   s) > 0) or
    (Pos('firebrazier', s) > 0) or
    (Pos('fxfire',      s) > 0) or
    (Pos('fxcampfire',  s) > 0) or
    (Pos('firevol',     s) > 0) or
    (Pos('cookingpot',  s) > 0) or
    (Pos('cookpot',     s) > 0) or
    (Pos('cookingfire', s) > 0) or
    (Pos('cookingspit', s) > 0) or
    (Pos('cookspit',    s) > 0) or
    (Pos('blacksmithforge', s) > 0) or
    (Pos('forgefire',   s) > 0) or
    (Pos('skyforge',    s) > 0) or
    (Pos('smelter',     s) > 0) or
    (Pos('hearth',      s) > 0) or
    (Pos('bakeoven',    s) > 0) or
    (Pos('bakingoven',  s) > 0) or
    (Pos('kiln',        s) > 0) or
    // extra heat sources (parity with Survival Mode Improved)
    (Pos('steamfx',       s) > 0) or   // Dwemer steam FX emitters
    (Pos('steammystic',   s) > 0) or
    (Pos('dwesteam',      s) > 0) or
    (Pos('timewound',     s) > 0) or   // Throat of the World time wound
    (Pos('winterholdlight', s) > 0);   // Winterhold ambient lights
end;

// True if `items` (a FormList's FormIDs container) already holds `fid`.
function FlstHasFid(items: IInterface; fid: Cardinal): Boolean;
var
  i: Integer;
begin
  Result := False;
  for i := 0 to Pred(ElementCount(items)) do
    if GetNativeValue(ElementByIndex(items, i)) = fid then begin
      Result := True;
      Exit;
    end;
end;

// Append one record to a FormList (master-if-missing, dedup, skip broken fid).
function FlstAddRecord(items: IInterface; r: IwbMainRecord): Boolean;
var
  el : IInterface;
  fid: Cardinal;
begin
  Result := False;
  if not Assigned(r) then Exit;
  fid := GetLoadOrderFormID(r);
  if (fid = 0) or ((fid and $00FFFFFF) = 0) then Exit;
  if FlstHasFid(items, fid) then Exit;
  AddMasterIfMissing(tgt, GetFileName(GetFile(MasterOrSelf(r))));
  el := ElementAssign(items, HighInteger, nil, False);
  if not Assigned(el) then Exit;
  SetNativeValue(el, fid);
  Result := True;
end;

// Add explicit EditorIDs (space/comma-separated) from one file to a FormList.
procedure FlstAddEdids(items: IInterface; fname, sig, list: string; var found: Integer);
var
  src : IwbFile;
  sl  : TStringList;
  i   : Integer;
  r   : IwbMainRecord;
begin
  src := FileByName(fname);
  if not Assigned(src) then begin
    Say('    (file not loaded: ' + fname + ')');
    Exit;
  end;
  sl := TStringList.Create;
  try
    sl.CommaText := StringReplace(list, ' ', ',', [rfReplaceAll]);
    for i := 0 to Pred(sl.Count) do begin
      if sl[i] = '' then Continue;
      r := RecordByEDID(src, sig, sl[i]);
      if not Assigned(r) then begin
        Say('    ? not found: ' + sig + ' ' + sl[i] + ' in ' + fname);
        Continue;
      end;
      if FlstAddRecord(items, r) then begin
        Inc(found);
        Say('    + ' + sig + ' ' + sl[i] + '  (' + fname + ')');
      end;
    end;
  finally
    sl.Free;
  end;
end;

procedure ScanFireFile(src: IwbFile; items: IInterface; sigs: TStringList; var found: Integer);
var
  g  : IwbGroupRecord;
  r  : IwbMainRecord;
  i, k: Integer;
begin
  if not Assigned(src) then Exit;
  for k := 0 to Pred(sigs.Count) do begin
    g := GroupBySignature(src, sigs[k]);
    if not Assigned(g) then Continue;
    for i := 0 to Pred(ElementCount(g)) do begin
      r := ElementByIndex(g, i);
      if not LooksLikeFire(EditorID(r)) then Continue;
      if FlstAddRecord(items, r) then begin
        Inc(found);
        Say('    + ' + sigs[k] + ' ' + EditorID(r) + '  (' + GetFileName(src) + ')');
      end;
    end;
  end;
end;

procedure BuildFireList;
var
  flst : IwbMainRecord;
  grp  : IwbGroupRecord;
  items: IInterface;
  oldItems: IInterface;
  sigs : TStringList;
  found: Integer;
begin
  Say('');
  Say('--- FLST: fire sources ---');

  flst := RecordByEDID(tgt, 'FLST', PFX + 'FireSources');
  if Assigned(flst) then begin
    Inc(reused);
  end else begin
    grp := EnsureGroup('FLST');
    if not Assigned(grp) then Exit;
    flst := Add(grp, 'FLST', True);
    if not Assigned(flst) then begin
      Problem('не создался FLST');
      Exit;
    end;
    PutEdit(flst, 'EDID', PFX + 'FireSources');
    Inc(madeNew);
  end;
  Remember(PFX + 'FireSources', flst);

  // Always rebuild the contents: the LooksLikeFire mask changes between runs.
  oldItems := ElementByName(flst, 'FormIDs');
  if Assigned(oldItems) then
    Remove(oldItems);
  items := Add(flst, 'FormIDs', True);
  if not Assigned(items) then begin
    Problem('нет контейнера FormIDs в FLST');
    Exit;
  end;

  sigs := TStringList.Create;
  try
    sigs.Add('ACTI');
    sigs.Add('FURN');
    sigs.Add('STAT');
    // MSTT: a burning campfire is Campfire01LandBurning* / FXFire*, all
    // Moveable Static. STAT versions (*Off, *LandOff) are unlit placeholders.
    // Without MSTT the mod does not see an ordinary campfire.
    sigs.Add('MSTT');
    sigs.Add('LIGH');

    found := 0;
    ScanFireFile(FileByName('Skyrim.esm'),     items, sigs, found);
    ScanFireFile(FileByName('Dawnguard.esm'),  items, sigs, found);
    ScanFireFile(FileByName('HearthFires.esm'),items, sigs, found);
    ScanFireFile(FileByName('Dragonborn.esm'), items, sigs, found);

    // Explicit allowlist: parity with CC Survival Mode's Survival_WarmUpObjectsList.
    // Catches heat sources the EditorID mask misses (lighthouse brazier, magic
    // fire pillars, flaming debris, Sovngarde fire). FlstAddRecord dedups.
    Say('  + explicit heat-source allowlist (Survival Mode parity):');
    FlstAddEdids(items, 'Skyrim.esm', 'ACTI', 'SlighthouseActivator', found);
    FlstAddEdids(items, 'Skyrim.esm', 'MSTT',
      'FXFireSovngarde MGMagicFirePillar01 MGMagicFirePillarSmall FXFirePillar01 ' +
      'BFXBurningBeamAnim FXSmokeLargeClose01', found);
    FlstAddEdids(items, 'Dragonborn.esm', 'MSTT',
      'DLC2FXFlamingRockDebris DLC2FXFlamingRockDebrisSmall', found);

    if found = 0 then
      Problem('fire list empty - check that the masters are loaded');
    Say('  items added: ' + IntToStr(found));
    Say('  REVIEW BY EYE - list built from an EditorID mask, not meaning.');
  finally
    sigs.Free;
  end;
end;

// --- FLST: cold interiors ----------------------------------------------
// Locations (ice caves, glacial ruins) where "interior" is NOT cosy - cold
// still bites and a campfire only partly helps. Same set CC Survival Mode uses
// (Survival_ColdInteriorLocations + Survival_ColdInteriorCells, the two cells
// folded to their parent locations). The controller walks the player's
// location parent chain against this list.
procedure BuildColdInteriors;
var
  flst : IwbMainRecord;
  grp  : IwbGroupRecord;
  items, old: IInterface;
  found: Integer;
begin
  Say('');
  Say('--- FLST: cold interiors ---');

  flst := RecordByEDID(tgt, 'FLST', PFX + 'ColdInteriors');
  if Assigned(flst) then begin
    Inc(reused);
  end else begin
    grp := EnsureGroup('FLST');
    if not Assigned(grp) then Exit;
    flst := Add(grp, 'FLST', True);
    if not Assigned(flst) then begin
      Problem('ColdInteriors FLST not created');
      Exit;
    end;
    PutEdit(flst, 'EDID', PFX + 'ColdInteriors');
    Inc(madeNew);
  end;
  Remember(PFX + 'ColdInteriors', flst);

  old := ElementByName(flst, 'FormIDs');
  if Assigned(old) then Remove(old);
  items := Add(flst, 'FormIDs', True);
  if not Assigned(items) then begin
    Problem('ColdInteriors: no FormIDs container');
    Exit;
  end;

  found := 0;
  FlstAddEdids(items, 'Skyrim.esm', 'LCTN',
    'BleakcoastCaveLocation BonechillPassageLocation ColdRockPassLocation ' +
    'DuskglowCreviceLocation FrostflowLighthouseLocation GreywaterGrottoLocation ' +
    'HaemarsShameLocation HobsFallCaveLocation SeptimusSignusOutpostLocation ' +
    'SightlessPitLocation SouthfringeSanctumLocation SteepfallBurrowLocation ' +
    'StillbornCaveLocation YngvildLocation AlftandLocation ForsakenCaveLocation', found);
  FlstAddEdids(items, 'Dawnguard.esm', 'LCTN', 'DLC1GlacialCreviceLocation', found);
  FlstAddEdids(items, 'Dragonborn.esm', 'LCTN',
    'DLC2AltarofThrondLocation DLC2BenkongerikeLocation DLC2BristlebackCaveLocation ' +
    'DLC2FrosselLocation DLC2GlacialCaveLocation', found);

  if found = 0 then
    Problem('cold-interiors list empty - masters not loaded?');
  Say('  items added: ' + IntToStr(found));
end;

// Strip inherited baggage from a record copied off a vanilla template.
// wbCopyElementToFile copies the whole record: VMAD (template scripts stay
// bound and RUN), DNAM (template description), KSIZ/KWDA (keywords like
// MagicAlchHarmful), MDOB. All removed right after the copy.

procedure DropElement(rec: IwbMainRecord; sig: string);
var
  el: IInterface;
begin
  el := ElementBySignature(rec, sig);
  if Assigned(el) then begin
    Remove(el);
    Say('    dropped ' + sig + ' from ' + EditorID(rec));
  end;
end;

procedure ScrubTemplate(rec: IwbMainRecord);
begin
  if not Assigned(rec) then Exit;
  DropElement(rec, 'VMAD');   // template scripts - the important one
  DropElement(rec, 'DNAM');   // template description
  DropElement(rec, 'KSIZ');
  DropElement(rec, 'KWDA');
  DropElement(rec, 'MDOB');
end;

// Effect flags. Copying a template also copies its flags; an early version
// picked shieldChargeDamageStamina (a DAMAGE effect, Hostile + Detrimental),
// so the Value Modifier hit the current stat instead of the max and penalties
// did nothing. Flags are now set from a known-good template + explicit bits.

// Resolve the MGEF DATA\Flags element. The compound path
// 'Magic Effect Data\DATA\Flags' resolves to nil in JvInterpreter on records
// that were just wbCopyElementToFile'd and not yet touched; the raw DATA
// subrecord (by signature) always resolves. Try both.
function MgefFlags(rec: IwbMainRecord): IInterface;
var
  data: IInterface;
begin
  Result := ElementByPath(rec, 'Magic Effect Data\DATA\Flags');
  if Assigned(Result) then Exit;
  data := ElementBySignature(rec, 'DATA');
  if Assigned(data) then Result := ElementByPath(data, 'Flags');
end;

// Lists set flags by name so the result shows in the generator log.
// Index walk only: unset flags are not subelements, so no name lookup.
function FlagsOf(rec: IwbMainRecord): string;
var
  fl, sub: IInterface;
  i: Integer;
begin
  Result := '';
  fl := MgefFlags(rec);
  if not Assigned(fl) then begin
    Result := '<no Flags>';
    Exit;
  end;
  for i := 0 to Pred(ElementCount(fl)) do begin
    sub := ElementByIndex(fl, i);
    if GetEditValue(sub) = '1' then begin
      if Result <> '' then Result := Result + ', ';
      Result := Result + Name(sub);
    end;
  end;
  if Result = '' then Result := '<empty>';
end;

// ORs in the Detrimental bit and verifies it by reading flags back by name
// (an unset flag is not a subelement, so it cannot be set by name).
procedure AddDetrimental(rec: IwbMainRecord);
var
  fl: IInterface;
begin
  if not MARK_DETRIMENTAL then Exit;
  if not Assigned(rec) then Exit;

  fl := MgefFlags(rec);
  if not Assigned(fl) then begin
    Problem('no Flags on ' + EditorID(rec));
    Exit;
  end;

  SetNativeValue(fl, GetNativeValue(fl) or FLAG_DETRIMENTAL);

  if Pos('Detrimental', FlagsOf(rec)) = 0 then
    Problem('bit ' + IntToHex(FLAG_DETRIMENTAL, 8) + ' on ' + EditorID(rec)
          + ' is not Detrimental. Got: ' + FlagsOf(rec));
end;

// Copies the WHOLE flag set from the template as one integer. Per-name setting
// fails: an unset flag has no subelement (cannot add Recover by name), and
// clearing a flag destroys its subelement, leaving a dangling ref that crashes
// SSEEdit on the next read. Template BladesAbBlessing (vanilla Fortify Health)
// has Recover + No Duration + No Area - exactly what a pool modifier needs.
// Recover is the key one: without it a Value Modifier hits the current value,
// not the max.
procedure CopyFlagsFrom(rec: IwbMainRecord; tpl: IwbMainRecord);
var
  src, dst: IInterface;
begin
  if not Assigned(rec) or not Assigned(tpl) then Exit;

  src := MgefFlags(tpl);
  dst := MgefFlags(rec);

  if not Assigned(src) then begin
    Problem('template ' + EditorID(tpl) + ' has no Flags');
    Exit;
  end;
  if not Assigned(dst) then begin
    Problem(EditorID(rec) + ' has no Flags');
    Exit;
  end;

  SetNativeValue(dst, GetNativeValue(src));
end;

// "Hide in UI" = bit 0x8000 in Magic Effect Data\DATA\Flags. The monitor is
// pure plumbing; without this it shows in the active-effects list.
procedure HideInUI(rec: IwbMainRecord);
var
  el: IInterface;
  v : Cardinal;
begin
  if not Assigned(rec) then Exit;

  el := MgefFlags(rec);
  if not Assigned(el) then begin
    Problem('no Flags path on ' + EditorID(rec) + ' - effect stays visible');
    Exit;
  end;

  v := GetNativeValue(el);
  SetNativeValue(el, v or $00008000);
  Say('    hidden from UI: ' + EditorID(rec));
end;

// Clear the "Hide in UI" bit - for a face MGEF cloned off a hidden library one.
procedure ShowInUI(rec: IwbMainRecord);
var
  el: IInterface;
begin
  if not Assigned(rec) then Exit;
  el := MgefFlags(rec);
  if not Assigned(el) then begin
    Problem('no Flags path on ' + EditorID(rec));
    Exit;
  end;
  SetNativeValue(el, GetNativeValue(el) and not $00008000);
end;

// --- MGEF and SPEL: by copying a verified vanilla template --------------

function FindValueModifierTemplate: IwbMainRecord;
var
  src : IwbFile;
  grp : IwbGroupRecord;
  r   : IwbMainRecord;
  i   : Integer;
  arch, cast, deliv: string;
begin
  Result := nil;

  src := FileByName('Skyrim.esm');
  if not Assigned(src) then begin
    Problem('Skyrim.esm not loaded - cannot find MGEF template');
    Exit;
  end;

  // Preferred: BladesAbBlessing (vanilla Fortify Health, clean flags: Recover,
  // No Duration, No Area). The archetype scan below is a fallback - it takes
  // the FIRST match, which can easily be a damage effect with Detrimental.
  Result := RecordByEDID(src, 'MGEF', 'BladesAbBlessing');
  if Assigned(Result) then begin
    Say('  MGEF template: ' + EditorID(Result) + '  (reference Fortify)');
    Exit;
  end;
  Say('  BladesAbBlessing not found, scanning by archetype');

  grp := GroupBySignature(src, 'MGEF');
  if not Assigned(grp) then begin
    Problem('no MGEF group in Skyrim.esm');
    Exit;
  end;

  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);

    arch  := GetElementEditValues(r, 'Magic Effect Data\DATA\Archtype');
    cast  := GetElementEditValues(r, 'Magic Effect Data\DATA\Casting Type');
    deliv := GetElementEditValues(r, 'Magic Effect Data\DATA\Delivery');

    if SameText(arch, 'Value Modifier')
       and SameText(cast, 'Constant Effect')
       and SameText(deliv, 'Self') then begin
      Result := r;
      Say('  MGEF template: ' + EditorID(r) + '  [' + arch + ' / ' + cast + ' / ' + deliv + ']');
      Exit;
    end;
  end;

  Problem('no vanilla MGEF (Value Modifier / Constant Effect / Self)');
end;

// DNAM is set on purpose: without a description the game builds the tooltip
// itself and prints the magnitude as an unsigned int
// ("+4294967290 ... Health" instead of "-6"). With our own description the
// engine shows it and omits the number.
function AddMgef(tpl: IwbMainRecord; edid: string; fullName: string;
                 actorValue: string; descr: string): IwbMainRecord;
var
  fresh: Boolean;
begin
  Result := RecordByEDID(tgt, 'MGEF', edid);
  fresh := not Assigned(Result);

  if fresh then begin
    Result := wbCopyElementToFile(tpl, tgt, True, True);
    if not Assigned(Result) then begin
      Problem('не скопировался MGEF ' + edid);
      Exit;
    end;
  end;

  // Runs for both new and existing records, else a rerun would not fix
  // names/descriptions on effects already created.
  ScrubTemplate(Result);

  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'FULL', fullName);
  PutEdit(Result, 'DNAM', descr);

  // Target pool. Magnitude is not set here - the script sets it at runtime
  // via SetNthEffectMagnitude.
  PutEdit(Result, 'Magic Effect Data\DATA\Actor Value', actorValue);

  CopyFlagsFrom(Result, tpl);
  AddDetrimental(Result);
  Say('    ' + edid + ' -> ' + FlagsOf(Result));

  Remember(edid, Result);
  if fresh then Inc(madeNew) else Inc(reused);
end;

// --- balance pass: penalty MGEF library --------------------------------
// Copy a named MGEF from a master AS A NEW record: its Actor Value + archetype
// come from the source (correct by construction). Scrub script/conditions/sound,
// rename, optionally flip the Detrimental bit. FULL/DNAM come from strings.txt
// keyed lib.<stem>.full / .dnam where stem = newEdid minus the "_RSL_Mgef"
// prefix. Magnitude is baked per stage on the SPEL's EFIT, not here.
function CopyVanillaMgef(srcFile, srcEdid, newEdid: string;
                         flipDetrimental: Boolean): IwbMainRecord;
var
  src, old : IwbMainRecord;
  stem : string;
begin
  stem := Copy(newEdid, Length(PFX) + 5, Length(newEdid));   // strip "<PFX>Mgef"
  src := RecordByEDID(FileByName(srcFile), 'MGEF', srcEdid);
  if not Assigned(src) then begin
    Problem('CopyVanillaMgef: source not found ' + srcFile + ':' + srcEdid);
    Exit;
  end;
  AddMasterIfMissing(tgt, srcFile);

  // Always drop + deep-recopy. Reusing a record kept whatever a previous
  // (buggy) run left in DATA - a shallow copy's unnavigable DATA, or an Actor
  // Value clobbered to "Aggression" by a bad priming write. A fresh deep copy
  // straight from the source is the only state we can trust. FormID churn is
  // harmless: the SPELs that reference the library are rebuilt in the same run.
  old := RecordByEDID(tgt, 'MGEF', newEdid);
  if Assigned(old) then Remove(old);
  Result := wbCopyElementToFile(src, tgt, True, True);
  if not Assigned(Result) then begin
    Problem('CopyVanillaMgef: not copied ' + newEdid);
    Exit;
  end;
  Inc(madeNew);

  ScrubTemplate(Result);           // VMAD/DNAM/KSIZ/KWDA/MDOB
  DropElement(Result, 'CTDA');     // some RFAB sources carry Peryite conditions
  DropElement(Result, 'SNDD');     // no per-effect sounds on a static debuff
  PutEdit(Result, 'EDID', newEdid);
  PutEdit(Result, 'FULL', L('lib.' + stem + '.full'));
  PutEdit(Result, 'DNAM', L('lib.' + stem + '.dnam'));

  // Materialize the named DATA path so the flag write below persists. On a
  // freshly-copied record 'Magic Effect Data\DATA\Flags' resolves to nil until
  // a PutEdit on that compound path forces the tree open (same trick AddMgef
  // uses with Actor Value). Casting Type / Delivery are Constant Effect / Self
  // for every library effect, so writing them is a safe no-op that also fixes
  // any wrong value a scrubbed source carried - and it CANNOT clobber an enum
  // (unlike blindly echoing Actor Value, which set it to index 0 when the read
  // came back empty).
  PutEdit(Result, 'Magic Effect Data\DATA\Casting Type', 'Constant Effect');
  PutEdit(Result, 'Magic Effect Data\DATA\Delivery',     'Self');

  // Library MGEFs are pure mechanics - hidden in the active-effects UI. Each
  // stage SPEL shows a single visible "face" MGEF (BuildFace) cloned off the
  // first of these, carrying the combined description.
  HideInUI(Result);

  if flipDetrimental then
    AddDetrimental(Result);   // proven: ORs the bit, verifies by name via FlagsOf

  Remember(newEdid, Result);
end;

// The whole reusable library. One MGEF per (actor value, sense). Magnitudes
// live on each stage SPEL's EFIT (BuildDiseases / CloneRfabDisease).
procedure BuildPenaltyLib;
begin
  Say('');
  Say('--- penalty MGEF library ---');
  // % regeneration (RateMult) - Detrimental subtracts %, flat *Rate untouched
  CopyVanillaMgef('Skyrim.esm', 'AbDamageMagickaRate',        PFX + 'MgefMagRegen',   False);
  CopyVanillaMgef('Skyrim.esm', 'AbDamageStaminaRateVisible', PFX + 'MgefStamRegen',  False);
  CopyVanillaMgef('Skyrim.esm', 'AbDamageHealRateVisible',    PFX + 'MgefHealRegen',  False);
  // speeds (Peak Value Mod - no stacking)
  CopyVanillaMgef('Skyrim.esm', 'AlchDamageSpeed',            PFX + 'MgefSpeed',      False);
  CopyVanillaMgef('RFAB.esp',   'RFAB_Effect_PeryiteWitbane_DecreaseAttackSpeed', PFX + 'MgefWeapSpeed', False);
  CopyVanillaMgef('RFAB.esp',   'RFAB_Effect_PeryiteWitbane_WeaknessCastSpeed',   PFX + 'MgefCastSpeed', False);
  // vanilla Fortify -> our debuff (flip Detrimental)
  CopyVanillaMgef('Skyrim.esm', 'AbFortifySneak',             PFX + 'MgefSneak',      True);
  CopyVanillaMgef('Skyrim.esm', 'AbResistMagic',              PFX + 'MgefMagicWeak',  True);
  CopyVanillaMgef('Skyrim.esm', 'AbFortifyCarryWeight',       PFX + 'MgefCarry',      True);
  CopyVanillaMgef('Skyrim.esm', 'AbFortifyHealRate',          PFX + 'MgefHealDrain',  True);
  CopyVanillaMgef('RFAB.esp',   'RFAB_Effect_PeryiteAtaxia_ResistStagger_Hide',    PFX + 'MgefWeakPoise', True);
  // max pools (% of base, magnitude set at runtime by SetStage on effect 0)
  CopyVanillaMgef('Skyrim.esm', 'BladesAbBlessing',           PFX + 'MgefMaxHealth',  True);
  CopyVanillaMgef('Skyrim.esm', 'AlchFortifyMagicka',         PFX + 'MgefMaxMagicka', True);
  CopyVanillaMgef('Skyrim.esm', 'AlchFortifyStamina',         PFX + 'MgefMaxStamina', True);
  // school cost (цена = 100 - Mod; Detrimental -> Mod negative -> costs more)
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyAlteration',      PFX + 'MgefCostAlt',    True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyConjuration',     PFX + 'MgefCostConj',   True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyDestruction',     PFX + 'MgefCostDest',   True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyIllusion',        PFX + 'MgefCostIllu',   True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyRestoration',     PFX + 'MgefCostRest',   True);
  // Peryite bonuses for wrapper stages 2/3 (stay Fortify)
  CopyVanillaMgef('Skyrim.esm', 'AbResistFrost',              PFX + 'MgefBonusFrost', False);
  CopyVanillaMgef('RFAB.esp',   'RFAB_Effect_PeryiteAtaxia_ResistStagger_Hide',   PFX + 'MgefBonusPoise', False);
  CopyVanillaMgef('RFAB.esp',   'RFAB_Effect_PeryiteRockjoint_FortifyArmorRating', PFX + 'MgefBonusArmor', False);
  Say('  penalty library: 21 MGEF');
end;

function FindAbilityTemplate: IwbMainRecord;
var
  src : IwbFile;
  grp : IwbGroupRecord;
  r   : IwbMainRecord;
  i   : Integer;
  styp, cast, deliv: string;
begin
  Result := nil;

  src := FileByName('Skyrim.esm');
  if not Assigned(src) then Exit;

  grp := GroupBySignature(src, 'SPEL');
  if not Assigned(grp) then begin
    Problem('в Skyrim.esm нет группы SPEL');
    Exit;
  end;

  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);

    styp  := GetElementEditValues(r, 'SPIT\Type');
    cast  := GetElementEditValues(r, 'SPIT\Cast Type');
    deliv := GetElementEditValues(r, 'SPIT\Target Type');

    if SameText(styp, 'Ability')
       and SameText(cast, 'Constant Effect')
       and SameText(deliv, 'Self') then begin
      Result := r;
      Say('  SPEL template: ' + EditorID(r) + '  [' + styp + ' / ' + cast + ' / ' + deliv + ']');
      Exit;
    end;
  end;

  Problem('no vanilla ability (Ability / Constant Effect / Self)');
end;

// Note: the xEdit field is "Archtype" (no e) - a typo in xEdit's record defs,
// do not "fix" it. Effect order matters: 0 Health, 1 Magicka, 2 Stamina,
// 3 SpeedMult; _RSL_Controller.ApplyAxis relies on these indices with
// SetNthEffectMagnitude. Top-level on purpose: the xEdit script engine has no
// nested procedures.
procedure AttachEffect(effects: IInterface; mgef: IwbMainRecord; owner: string);
var
  e: IInterface;
begin
  if not Assigned(mgef) then begin
    Problem('nothing to attach to ' + owner + ': effect not created');
    Exit;
  end;
  e := ElementAssign(effects, HighInteger, nil, False);
  if not Assigned(e) then begin
    Problem('effect not added to ' + owner);
    Exit;
  end;
  PutNative(e, 'EFID', GetLoadOrderFormID(mgef));
  PutNative(e, 'EFIT\Magnitude', 0.0);
  PutNative(e, 'EFIT\Area',      0);
  PutNative(e, 'EFIT\Duration',  0);
end;

// Effect order matters: 0 Health, 1 Magicka, 2 Stamina, 3 SpeedMult.
// The effect list is rebuilt every run (new and existing records) so an
// upgrade run reaches abilities already created.
function AddAbility(tpl: IwbMainRecord; edid: string; fullName: string;
                    mH: IwbMainRecord; mM: IwbMainRecord; mS: IwbMainRecord;
                    mSpeed: IwbMainRecord): IwbMainRecord;
var
  effects: IInterface;
  fresh  : Boolean;
begin
  Result := RecordByEDID(tgt, 'SPEL', edid);
  fresh := not Assigned(Result);

  if fresh then begin
    Result := wbCopyElementToFile(tpl, tgt, True, True);
    if not Assigned(Result) then begin
      Problem('не скопировался SPEL ' + edid);
      Exit;
    end;
  end;

  ScrubTemplate(Result);
  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'FULL', fullName);

  effects := ElementByName(Result, 'Effects');
  if not Assigned(effects) then begin
    Problem('no Effects container in ' + edid);
    Exit;
  end;

  while ElementCount(effects) > 0 do
    RemoveByIndex(effects, 0, True);

  AttachEffect(effects, mH,     edid);
  AttachEffect(effects, mM,     edid);
  AttachEffect(effects, mS,     edid);
  AttachEffect(effects, mSpeed, edid);

  Remember(edid, Result);
  if fresh then Inc(madeNew) else Inc(reused);
end;

procedure BuildEffectsAndSpells;
var
  mgefTpl, spelTpl: IwbMainRecord;
  sH, sM, sS      : IwbMainRecord;
  hH, hM, hS      : IwbMainRecord;
  cH, cM, cS      : IwbMainRecord;
  sSpeed, hSpeed, cSpeed : IwbMainRecord;
begin
  Say('');
  Say('--- MGEF: effects ---');

  mgefTpl := FindValueModifierTemplate;
  if not Assigned(mgefTpl) then begin
    Say('  no template - skipping effects and spells');
    Exit;
  end;

  // ONE visible line per axis (the primary pool cap, <mag> live = the flat
  // points the controller applies). The 3 cross debuffs (other pools + speed)
  // work but are hidden; their design caps go in the primary line as text.
  // RFAB style: no minus sign, "<N>" for every metric. Text: strings.txt,
  // axis.<Name>.full / .dnam; %P/%C/%S -> penalty defaults via ExpandP.

  // Sleep: primary = Magicka
  sH := AddMgef(mgefTpl, PFX + 'MgefSleepHealth',  L('axis.SleepHealth.full'),  'Health',  L('axis.SleepHealth.dnam'));
  sM := AddMgef(mgefTpl, PFX + 'MgefSleepMagicka', L('axis.SleepMagicka.full'), 'Magicka', ExpandP(L('axis.SleepMagicka.dnam')));
  sS := AddMgef(mgefTpl, PFX + 'MgefSleepStamina', L('axis.SleepStamina.full'), 'Stamina', L('axis.SleepStamina.dnam'));

  // Hunger: primary = Stamina
  hH := AddMgef(mgefTpl, PFX + 'MgefHungerHealth', L('axis.HungerHealth.full'),  'Health',  L('axis.HungerHealth.dnam'));
  hM := AddMgef(mgefTpl, PFX + 'MgefHungerMagicka',L('axis.HungerMagicka.full'), 'Magicka', L('axis.HungerMagicka.dnam'));
  hS := AddMgef(mgefTpl, PFX + 'MgefHungerStamina',L('axis.HungerStamina.full'), 'Stamina', ExpandP(L('axis.HungerStamina.dnam')));

  // Cold: primary = Health
  cH := AddMgef(mgefTpl, PFX + 'MgefColdHealth',   L('axis.ColdHealth.full'),   'Health',  ExpandP(L('axis.ColdHealth.dnam')));
  cM := AddMgef(mgefTpl, PFX + 'MgefColdMagicka',  L('axis.ColdMagicka.full'),  'Magicka', L('axis.ColdMagicka.dnam'));
  cS := AddMgef(mgefTpl, PFX + 'MgefColdStamina',  L('axis.ColdStamina.full'),  'Stamina', L('axis.ColdStamina.dnam'));

  // Movement speed - a cross component from each axis. The xEdit AV is
  // "Speed Mult" (with a space); "SpeedMult" raises EConvertError.
  sSpeed := AddMgef(mgefTpl, PFX + 'MgefSleepSpeed',  L('axis.SleepSpeed.full'),  'Speed Mult', L('axis.SleepSpeed.dnam'));
  hSpeed := AddMgef(mgefTpl, PFX + 'MgefHungerSpeed', L('axis.HungerSpeed.full'), 'Speed Mult', L('axis.HungerSpeed.dnam'));
  cSpeed := AddMgef(mgefTpl, PFX + 'MgefColdSpeed',   L('axis.ColdSpeed.full'),   'Speed Mult', L('axis.ColdSpeed.dnam'));

  // Hide the cross debuffs; only the primary pool line of each axis shows.
  HideInUI(sH);  HideInUI(sS);  HideInUI(sSpeed);
  HideInUI(hH);  HideInUI(hM);  HideInUI(hSpeed);
  HideInUI(cM);  HideInUI(cS);  HideInUI(cSpeed);

  Say('');
  Say('--- SPEL: constant abilities ---');

  spelTpl := FindAbilityTemplate;
  if not Assigned(spelTpl) then begin
    Say('  no template - skipping spells');
    Exit;
  end;

  AddAbility(spelTpl, PFX + 'AbSleep',  L('axis.spell.Sleep'),  sH, sM, sS, sSpeed);
  AddAbility(spelTpl, PFX + 'AbHunger', L('axis.spell.Hunger'), hH, hM, hS, hSpeed);
  AddAbility(spelTpl, PFX + 'AbCold',   L('axis.spell.Cold'),   cH, cM, cS, cSpeed);
end;

// --- diseases (WS5) ----------------------------------------------------

// Vanilla SPEL with SPIT\Type = Disease. The engine's Cure Disease / shrine
// blessings remove Disease-type spells automatically; the controller only
// watches for the removal to notify the player.
function FindDiseaseTemplate: IwbMainRecord;
var
  src : IwbFile;
  grp : IwbGroupRecord;
  r   : IwbMainRecord;
  i   : Integer;
begin
  Result := nil;
  src := FileByName('Skyrim.esm');
  if not Assigned(src) then Exit;
  grp := GroupBySignature(src, 'SPEL');
  if not Assigned(grp) then Exit;

  // Prefer a real contagious disease (Constant Effect / Self). The first
  // Type=Disease SPEL in Skyrim.esm is TrapDiseaseWitbane, whose Target Type is
  // Touch (a spike-trap delivers it) - copied as a template it produces stage
  // spells that never apply when AddSpell'd. NormalizeSpit fixes it anyway, but
  // start from something sane.
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if SameText(GetElementEditValues(r, 'SPIT\Type'), 'Disease')
       and SameText(GetElementEditValues(r, 'SPIT\Cast Type'), 'Constant Effect')
       and SameText(GetElementEditValues(r, 'SPIT\Target Type'), 'Self') then begin
      Result := r;
      Say('  disease SPEL template: ' + EditorID(r) + ' (Constant/Self)');
      Exit;
    end;
  end;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if SameText(GetElementEditValues(r, 'SPIT\Type'), 'Disease') then begin
      Result := r;
      Say('  disease SPEL template: ' + EditorID(r) + ' (fallback - NormalizeSpit will fix SPIT)');
      Exit;
    end;
  end;
  Problem('no vanilla SPEL with SPIT\Type = Disease');
end;

// Force a stage SPEL's SPIT to the shape the engine needs for AddSpell:
// spitType ('Disease' or 'Ability') / Constant Effect / Self. Drops ETYP
// (equipment slot) and the template's inherited DESC. Templates copied from
// trap or hostile spells carry the wrong Target Type / description.
procedure NormalizeSpit(rec: IwbMainRecord; spitType: string);
begin
  if not Assigned(rec) then Exit;
  PutEdit(rec, 'SPIT\Type',        spitType);
  PutEdit(rec, 'SPIT\Cast Type',   'Constant Effect');
  PutEdit(rec, 'SPIT\Target Type', 'Self');
  DropElement(rec, 'ETYP');
end;

// MESG record with the Message Box flag OFF -> shown by Message.Show() as a
// top-left notification (same UI as Debug.Notification) but the text lives in
// the plugin, so the Russian build renders Cyrillic correctly.
function AddMsg(edid: string; body: string): IwbMainRecord;
var
  grp: IwbGroupRecord;
begin
  Result := RecordByEDID(tgt, 'MESG', edid);
  if Assigned(Result) then begin
    PutEdit(Result, 'DESC', body);
    PutNative(Result, 'DNAM', 0);          // 0 = not a message box
    Inc(reused);
    Remember(edid, Result);
    Exit;
  end;

  grp := EnsureGroup('MESG');
  if not Assigned(grp) then Exit;
  Result := Add(grp, 'MESG', True);
  if not Assigned(Result) then begin
    Problem('MESG not created ' + edid);
    Exit;
  end;
  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'DESC', body);
  PutNative(Result, 'DNAM', 0);
  Remember(edid, Result);
  Inc(madeNew);
end;

// A stage SPEL built from the penalty library. `effSpec` is a comma list of
// "MgefStem=mag" (mag = integer: % for RateMult/school-cost, points for
// SpeedMult/Sneak/CarryWeight/max-pool). Effects appended in order. Empty
// spec is allowed (controller-side-only stages) but every real stage should
// carry at least one effect so it shows in the active-effects list.
// StrToInt is not reliably in JvInterpreter - parse "[-]digits" by hand.
function SpecInt(s: string): Integer;
var
  i, sign: Integer;
begin
  Result := 0;
  sign := 1;
  s := Trim(s);
  i := 1;
  if (Length(s) > 0) and (s[1] = '-') then begin sign := -1; i := 2; end;
  while i <= Length(s) do begin
    if (s[i] >= '0') and (s[i] <= '9') then
      Result := Result * 10 + (Ord(s[i]) - Ord('0'))
    else
      Problem('SpecInt: non-digit in "' + s + '"');
    Inc(i);
  end;
  Result := Result * sign;
end;

// stem -> stat clause, for descriptions built straight from the spec so a
// magnitude is written once and shown everywhere. RFAB style: no minus sign,
// the verb carries the direction. Phrases come from strings.txt (phrase.<stem>,
// or phrase.MgefCost for any school-cost stem); each is a full capitalised
// clause, joined with ". " by SpecToText.
function StemPhrase(stem: string): string;
begin
  if Pos('MgefCost', stem) = 1 then Result := L('phrase.MgefCost')
  else Result := L('phrase.' + stem);
end;

// Unit token after the number (points / per-second / percent). No trailing
// period - SpecToText joins clauses with ". " and adds a final ".".
function StemUnit(stem: string): string;
begin
  if (stem = 'MgefSneak') or (stem = 'MgefCarry') or (stem = 'MgefMaxHealth')
     or (stem = 'MgefMaxMagicka') or (stem = 'MgefMaxStamina')
     or (stem = 'MgefBonusArmor') then Result := L('phrase.unit.points')
  else if stem = 'MgefHealDrain' then Result := L('phrase.unit.perSec')
  else Result := L('phrase.unit.percent');
end;

// A number in angle brackets: RFAB/Requiem's highlight for every metric - the
// active-effects parser renders "<35>" (like "<mag>" / "<Global=X>") in the
// bright colour. <font color> does NOT work in this build; <N> does.
function Hi(n: Integer): string;
begin
  Result := '<' + IntToStr(n) + '>';
end;

// RFAB-style sentence list from a "stem=mag,..." spec. Tokens before fromIdx
// skipped. magTagFirst: the first clause uses <mag> (live value of the effect
// it is attached to). Repeated MgefCost* tokens collapse to one
// "all schools cost more" clause (phrase.costAll).
function SpecToText(effSpec: string; fromIdx: Integer; magTagFirst: Boolean): string;
var
  sl : TStringList;
  i, p, mag, rendered: Integer;
  stem, num, piece: string;
  costDone: Boolean;
begin
  Result := '';
  rendered := 0;
  costDone := False;
  sl := TStringList.Create;
  try
    sl.CommaText := effSpec;
    for i := 0 to Pred(sl.Count) do begin
      if i < fromIdx then Continue;
      if sl[i] = '' then Continue;
      p := Pos('=', sl[i]);
      if p = 0 then Continue;
      stem := Copy(sl[i], 1, p - 1);
      mag  := SpecInt(Copy(sl[i], p + 1, Length(sl[i])));

      if Pos('MgefCost', stem) = 1 then begin
        if costDone then Continue;
        costDone := True;
        if magTagFirst and (rendered = 0) then num := '<mag>' else num := Hi(mag);
        piece := L('phrase.costAll') + ' ' + num + L('phrase.unit.percent');
      end else begin
        if magTagFirst and (rendered = 0) then num := '<mag>' else num := Hi(mag);
        piece := StemPhrase(stem) + L('phrase.on') + ' ' + num + StemUnit(stem);
      end;

      if Result = '' then Result := piece
      else Result := Result + '. ' + piece;
      rendered := rendered + 1;
    end;
  finally
    sl.Free;
  end;
  if Result <> '' then Result := Result + '.';
end;

// Append library MGEF effects to a SPEL's Effects container. effSpec = comma
// list of "MgefStem=mag"; tokens before fromIdx are skipped (the stage's first
// effect is a visible "face" MGEF built separately).
procedure AppendLibEffectsFrom(effects: IInterface; effSpec, ctx: string; fromIdx: Integer);
var
  sl : TStringList;
  i, p, mag: Integer;
  stem: string;
  mgef: IwbMainRecord;
  e   : IInterface;
begin
  if not Assigned(effects) then Exit;
  sl := TStringList.Create;
  try
    sl.CommaText := effSpec;
    for i := 0 to Pred(sl.Count) do begin
      if i < fromIdx then Continue;
      if sl[i] = '' then Continue;
      p := Pos('=', sl[i]);
      if p = 0 then begin Problem('AppendLibEffects: bad token "' + sl[i] + '" in ' + ctx); Continue; end;
      stem := Copy(sl[i], 1, p - 1);
      mag  := SpecInt(Copy(sl[i], p + 1, Length(sl[i])));
      mgef := RecordByEDID(tgt, 'MGEF', PFX + stem);
      if not Assigned(mgef) then begin
        Problem('AppendLibEffects: library MGEF missing: ' + PFX + stem + ' (' + ctx + ')');
        Continue;
      end;
      e := ElementAssign(effects, HighInteger, nil, False);
      PutNative(e, 'EFID', GetLoadOrderFormID(mgef));
      PutNative(e, 'EFIT\Magnitude', mag);
      PutNative(e, 'EFIT\Area',      0);
      PutNative(e, 'EFIT\Duration',  0);
    end;
  finally
    sl.Free;
  end;
end;

// The first token of a stage spec, e.g. "MgefSneak" and 35 from
// "MgefSneak=35,MgefSpeed=10". Empty stem if the spec is empty/malformed.
function FirstStem(effSpec: string): string;
var p, c: Integer;
begin
  Result := '';
  effSpec := Trim(effSpec);
  c := Pos(',', effSpec);
  if c > 0 then effSpec := Copy(effSpec, 1, c - 1);
  p := Pos('=', effSpec);
  if p > 0 then Result := Trim(Copy(effSpec, 1, p - 1));
end;

function FirstMag(effSpec: string): Integer;
var p, c: Integer;
begin
  Result := 0;
  effSpec := Trim(effSpec);
  c := Pos(',', effSpec);
  if c > 0 then effSpec := Copy(effSpec, 1, c - 1);
  p := Pos('=', effSpec);
  if p > 0 then Result := SpecInt(Copy(effSpec, p + 1, Length(effSpec)));
end;

// Visible "face" MGEF for one stage: cloned off the first spec token's (hidden)
// library MGEF, so it carries the right actor value + archetype and applies the
// primary penalty; renamed to the stage and given the full auto-built text. The
// stage SPEL's remaining effects stay hidden - one line in the active-effects UI.
function BuildFace(faceEdid, libStem, fullName, dnam: string): IwbMainRecord;
var
  lib, old: IwbMainRecord;
begin
  Result := nil;
  lib := RecordByEDID(tgt, 'MGEF', PFX + libStem);
  if not Assigned(lib) then begin
    Problem('BuildFace: no library MGEF ' + PFX + libStem + ' for ' + faceEdid);
    Exit;
  end;
  old := RecordByEDID(tgt, 'MGEF', faceEdid);
  if Assigned(old) then Remove(old);
  Result := wbCopyElementToFile(lib, tgt, True, True);
  if not Assigned(Result) then begin
    Problem('BuildFace: not copied ' + faceEdid);
    Exit;
  end;
  Inc(madeNew);
  PutEdit(Result, 'EDID', faceEdid);
  PutEdit(Result, 'FULL', fullName);
  PutEdit(Result, 'DNAM', dnam);
  PutEdit(Result, 'Magic Effect Data\DATA\Casting Type', 'Constant Effect');
  PutEdit(Result, 'Magic Effect Data\DATA\Delivery',     'Self');
  ShowInUI(Result);   // the library MGEF is hidden; the face must show
  Remember(faceEdid, Result);
end;

// spitType: 'Disease' (our 5) or 'Ability' (hypothermia). flavour is one plain
// sentence; the numeric penalty list is appended from the spec (SpecToText) so
// the magnitude lives in exactly one place. Effect 0 = a visible face MGEF
// carrying that text + the primary penalty; the rest of the spec stays hidden.
function AddStageSpell(tpl: IwbMainRecord; edid, fullName, spitType, flavour, effSpec: string): IwbMainRecord;
var
  effects, e: IInterface;
  fresh: Boolean;
  descr, stem, faceEdid: string;
  face: IwbMainRecord;
begin
  Result := RecordByEDID(tgt, 'SPEL', edid);
  fresh := not Assigned(Result);
  if fresh then begin
    Result := wbCopyElementToFile(tpl, tgt, True, True);
    if not Assigned(Result) then begin
      Problem('stage SPEL not copied ' + edid);
      Exit;
    end;
  end;
  ScrubTemplate(Result);
  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'FULL', fullName);
  NormalizeSpit(Result, spitType);   // Constant Effect / Self - trap template is Touch

  // SPEL DESC: plain baked numbers (no <mag> - a SPEL has no single magnitude).
  // Face MGEF DNAM: the tooltip actually shown; first number via <mag> (live).
  PutEdit(Result, 'DESC', Trim(flavour + ' ' + SpecToText(effSpec, 0, False)));

  effects := ElementByName(Result, 'Effects');
  while Assigned(effects) and (ElementCount(effects) > 0) do
    RemoveByIndex(effects, 0, True);

  stem := FirstStem(effSpec);
  if stem <> '' then begin
    descr := Trim(flavour + ' ' + SpecToText(effSpec, 0, True));
    faceEdid := PFX + 'Face' + Copy(edid, Length(PFX) + 1, Length(edid));
    face := BuildFace(faceEdid, stem, fullName, descr);
    if Assigned(face) then begin
      e := ElementAssign(effects, HighInteger, nil, False);
      PutNative(e, 'EFID', GetLoadOrderFormID(face));
      PutNative(e, 'EFIT\Magnitude', FirstMag(effSpec));
      PutNative(e, 'EFIT\Area', 0);
      PutNative(e, 'EFIT\Duration', 0);
    end;
    AppendLibEffectsFrom(effects, effSpec, edid, 1);   // rest stay hidden
  end else
    AppendLibEffectsFrom(effects, effSpec, edid, 0);

  Remember(edid, Result);
  if fresh then Inc(madeNew) else Inc(reused);
end;

// Three disease-type stage SPELs + 4 MESG. All text from strings.txt
// (dz.<key>.name.N / .flavour.N / .msg.*); the numeric penalties come from the
// per-stage spec args and are auto-appended by AddStageSpell.
procedure BuildDiseaseTriad(disTpl: IwbMainRecord; key, spec1, spec2, spec3: string);
begin
  AddStageSpell(disTpl, PFX + 'Disease' + key + '1', L('dz.' + key + '.name.1'), 'Disease', L('dz.' + key + '.flavour.1'), spec1);
  AddStageSpell(disTpl, PFX + 'Disease' + key + '2', L('dz.' + key + '.name.2'), 'Disease', L('dz.' + key + '.flavour.2'), spec2);
  AddStageSpell(disTpl, PFX + 'Disease' + key + '3', L('dz.' + key + '.name.3'), 'Disease', L('dz.' + key + '.flavour.3'), spec3);
  AddMsg(PFX + 'Msg' + key + '1',     L('dz.' + key + '.msg.contract'));
  AddMsg(PFX + 'Msg' + key + '2',     L('dz.' + key + '.msg.2'));
  AddMsg(PFX + 'Msg' + key + '3',     L('dz.' + key + '.msg.3'));
  AddMsg(PFX + 'Msg' + key + 'Cured', L('dz.' + key + '.msg.cured'));
end;

// Hypothermia: 3 Ability SPEL (NOT Disease - engine cures must not touch it).
// Stage-3 lockdown (Paralysis actor value), HP drain and wait-block are driven
// from _RSL_Controller - no extra records here.
procedure BuildHypothermia;
var
  spelTpl : IwbMainRecord;
begin
  Say('');
  Say('--- hypothermia (ability, not disease) ---');
  spelTpl := FindAbilityTemplate;
  if not Assigned(spelTpl) then begin
    Say('  no ability template - skipping hypothermia');
    Exit;
  end;

  // Stat effects from the penalty library. Stage 3 carries a token slow only -
  // paralysis + the quadratic HP drain are driven from _RSL_Controller.
  AddStageSpell(spelTpl, PFX + 'AbHypo1', L('hy.name.1'), 'Ability', L('hy.flavour.1'), 'MgefSpeed=10');
  AddStageSpell(spelTpl, PFX + 'AbHypo2', L('hy.name.2'), 'Ability', L('hy.flavour.2'), 'MgefSpeed=25,MgefWeapSpeed=10');
  AddStageSpell(spelTpl, PFX + 'AbHypo3', L('hy.name.3'), 'Ability', L('hy.flavour.3'), 'MgefSpeed=30,MgefWeapSpeed=20');

  AddMsg(PFX + 'MsgHypo1',      L('hy.msg.1'));
  AddMsg(PFX + 'MsgHypo2',      L('hy.msg.2'));
  AddMsg(PFX + 'MsgHypo3',      L('hy.msg.3'));
  AddMsg(PFX + 'MsgHypoCured',  L('hy.msg.cured'));
  AddMsg(PFX + 'MsgHypoNoRest', L('hy.msg.noRest'));
end;

// Our 5 own diseases, 3 stages each, effects from the penalty library
// (BuildPenaltyLib). Controller-side multipliers (cold-tolerance, sleep
// efficiency, hunger accrual, food restore) are NOT effects - see the balance
// spec in magical-seeking-garden.md.
procedure BuildDiseases;
var
  disTpl: IwbMainRecord;
begin
  Say('');
  Say('--- diseases (balance-pass effect lists) ---');
  disTpl := FindDiseaseTemplate;
  if not Assigned(disTpl) then begin
    Say('  no disease template - skipping');
    Exit;
  end;

  // Common cold -> pneumonia. + cold-tolerance x0.85/0.7/0.5 (controller-side).
  BuildDiseaseTriad(disTpl, 'ColdCommon',
    'MgefMagRegen=10',
    'MgefMagRegen=40,MgefStamRegen=25',
    'MgefMaxStamina=15,MgefMagRegen=70,MgefStamRegen=50');

  // Brown rot (draugr hits). + sleep efficiency x0.9/0.8/0.7 (controller-side).
  BuildDiseaseTriad(disTpl, 'BrownRot',
    'MgefHealRegen=25',
    'MgefHealRegen=60,MgefMaxHealth=10',
    'MgefHealRegen=100,MgefMaxHealth=20,MgefCarry=35');

  // Gutworm (troll hits). + food restore -25/-50/-80% and hunger accrual
  // x1.3/1.7/2.5 (controller-side).
  BuildDiseaseTriad(disTpl, 'Gutworm',
    'MgefStamRegen=15',
    'MgefStamRegen=40',
    'MgefStamRegen=75');

  // Green spore (slaughterfish hits) - the caster's disease, kept survivable.
  BuildDiseaseTriad(disTpl, 'Greenspore',
    'MgefCastSpeed=10,MgefMagRegen=20',
    'MgefCastSpeed=20,MgefMagRegen=45,MgefMaxMagicka=10',
    'MgefCastSpeed=35,MgefMagRegen=75,MgefMaxMagicka=25');

  // Food poisoning (raw food). Nausea -> weakness -> bacteremia.
  BuildDiseaseTriad(disTpl, 'FoodPoison',
    'MgefSneak=15',
    'MgefSneak=25,MgefSpeed=10,MgefStamRegen=40',
    'MgefSneak=35,MgefSpeed=10,MgefStamRegen=40,MgefHealDrain=1,MgefMagicWeak=15');

  // Elemental lesions (frostbite/burns from elemental magic + deep cold). The
  // bespoke P model lives in _RSL_Controller.AdvanceElemLesion; here it is just
  // three Disease-type stage SPELs. Penalties hit every build at once.
  BuildDiseaseTriad(disTpl, 'ElemLesion',
    'MgefWeapSpeed=10,MgefCastSpeed=10,MgefSneak=10',
    'MgefWeapSpeed=15,MgefCastSpeed=15,MgefSneak=20,MgefSpeed=10,MgefStamRegen=30',
    'MgefWeapSpeed=25,MgefCastSpeed=25,MgefSneak=30,MgefSpeed=15,MgefHealRegen=50,MgefMaxStamina=25,MgefWeakPoise=15');
end;

// --- Package 4: wrappers over RFAB's own diseases ----------------------
// Stage 1 = RFAB's own RFAB_Disease_X (untouched - its debuff + Peryite bonus
// live). Stages 2/3 = _RSL_Dz<key>{2,3}: a 1:1 copy of RFAB_Disease_X's whole
// effect list (debuff + Peryite CTDA effects + description), same SPIT
// Type=Disease from the template, only FULL renamed. The controller swaps
// base <-> our stage spell and drives P; a cure walks it back one stage
// (engine strips the current Type=Disease spell, our tick re-adds one lower).

procedure CopyEffectsFrom(dst, src: IwbMainRecord);
var
  d, s: IInterface;
  i   : Integer;
begin
  d := ElementByName(dst, 'Effects');
  s := ElementByName(src, 'Effects');
  if not Assigned(d) or not Assigned(s) then begin
    Problem('CopyEffectsFrom: missing Effects container');
    Exit;
  end;
  while ElementCount(d) > 0 do
    RemoveByIndex(d, 0, True);
  for i := 0 to Pred(ElementCount(s)) do
    if not Assigned(ElementAssign(d, HighInteger, ElementByIndex(s, i), False)) then
      Problem('CopyEffectsFrom: effect ' + IntToStr(i) + ' not copied');
end;

// Stage 2/3 wrapper SPEL = RFAB's stage-1 effects (copied 1:1, still visible
// under RFAB's own names) + Peryite-conditional effects scaled by peryiteMult
// (CTDA kept) + our extras. The extras collapse to one visible "face" line
// (the renamed stage + auto-built text); the remaining extras stay hidden.
function CloneRfabDisease(disTpl, src: IwbMainRecord;
  newEdid, newFull, flavour, extraSpec: string; peryiteMult: Real): IwbMainRecord;
var
  dst, face : IwbMainRecord;
  effs, eff, e: IInterface;
  i    : Integer;
  fresh: Boolean;
  descr, stem, faceEdid: string;
begin
  Result := nil;
  if not Assigned(src) then Exit;
  dst := RecordByEDID(tgt, 'SPEL', newEdid);
  fresh := not Assigned(dst);
  if fresh then begin
    dst := wbCopyElementToFile(disTpl, tgt, True, True);
    if not Assigned(dst) then begin
      Problem('CloneRfabDisease: SPEL not copied ' + newEdid);
      Exit;
    end;
  end;
  ScrubTemplate(dst);
  PutEdit(dst, 'EDID', newEdid);
  PutEdit(dst, 'FULL', newFull);
  NormalizeSpit(dst, 'Disease');   // trap template is Constant Effect / Touch

  PutEdit(dst, 'DESC', Trim(flavour + ' ' + L('wrap.aggravated') + ' ' + SpecToText(extraSpec, 0, False)));
  descr := Trim(flavour + ' ' + L('wrap.aggravated') + ' ' + SpecToText(extraSpec, 0, True));

  CopyEffectsFrom(dst, src);

  effs := ElementByName(dst, 'Effects');
  if Assigned(effs) and (peryiteMult <> 1.0) then
    for i := 0 to Pred(ElementCount(effs)) do begin
      eff := ElementByIndex(effs, i);
      // a Peryite bonus effect is the one carrying a CTDA condition
      if Assigned(ElementBySignature(eff, 'CTDA')) then
        PutNative(eff, 'EFIT\Magnitude',
          GetNativeValue(ElementByPath(eff, 'EFIT\Magnitude')) * peryiteMult);
    end;

  stem := FirstStem(extraSpec);
  if stem <> '' then begin
    faceEdid := PFX + 'Face' + Copy(newEdid, Length(PFX) + 1, Length(newEdid));
    face := BuildFace(faceEdid, stem, newFull, descr);
    if Assigned(face) then begin
      e := ElementAssign(effs, HighInteger, nil, False);
      PutNative(e, 'EFID', GetLoadOrderFormID(face));
      PutNative(e, 'EFIT\Magnitude', FirstMag(extraSpec));
      PutNative(e, 'EFIT\Area', 0);
      PutNative(e, 'EFIT\Duration', 0);
    end;
    AppendLibEffectsFrom(effs, extraSpec, newEdid, 1);
  end else
    AppendLibEffectsFrom(effs, extraSpec, newEdid, 0);

  Remember(newEdid, dst);
  if fresh then Inc(madeNew) else Inc(reused);
  Result := dst;
end;

procedure BuildOneRfabWrapper(disTpl: IwbMainRecord;
  srcFile, srcEdid, key, spec2, spec3: string);
var
  src: IwbMainRecord;
  baseName: string;
begin
  src := RecordByEDID(FileByName(srcFile), 'SPEL', srcEdid);
  if not Assigned(src) then begin
    Say('  ! source not found: ' + srcFile + ':' + srcEdid + ' - skipping ' + key);
    Exit;
  end;
  AddMasterIfMissing(tgt, srcFile);
  baseName := L('wrap.' + key + '.base');
  CloneRfabDisease(disTpl, src, PFX + 'Dz' + key + '2',
    L('wrap.progressive') + ' ' + baseName, L('wrap.' + key + '.flavour.2'), spec2, 1.25);
  CloneRfabDisease(disTpl, src, PFX + 'Dz' + key + '3',
    L('wrap.severe') + ' ' + baseName, L('wrap.' + key + '.flavour.3'), spec3, 1.5);
  AddMsg(PFX + 'MsgDz' + key + '2',     baseName + L('wrap.msg.2'));
  AddMsg(PFX + 'MsgDz' + key + '3',     baseName + L('wrap.msg.3'));
  AddMsg(PFX + 'MsgDz' + key + 'Cured', baseName + L('wrap.msg.cured'));
  Say('  wrapped ' + key + ' <- ' + srcEdid + '  "' + baseName + '"');
end;

procedure BuildRfabWrappers;
var
  disTpl: IwbMainRecord;
begin
  Say('');
  Say('--- RFAB disease wrappers (progressive stages 2/3) ---');
  disTpl := FindDiseaseTemplate;
  if not Assigned(disTpl) then begin
    Say('  no disease template - skipping wrappers');
    Exit;
  end;
  // extras = library effects appended on top of RFAB's stage-1 effects (which
  // are copied 1:1). Peryite-gated effects in the copy are scaled x1.25/1.5.
  // Magnitudes are first-pass - tune in playtest.
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'RFAB_Disease_Ataxia', 'AT',
    'MgefWeapSpeed=10',
    'MgefWeapSpeed=20,MgefSneak=20');
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'RFAB_Disease_Rockjoint', 'RJ',
    'MgefWeapSpeed=15',
    'MgefWeapSpeed=30,MgefSpeed=10');
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'RFAB_Disease_Witbane', 'WB',
    'MgefCastSpeed=5,MgefWeakPoise=10',
    'MgefCastSpeed=10,MgefWeapSpeed=5,MgefWeakPoise=20');
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'RFAB_Disease_Rattles', 'RA',
    'MgefMaxHealth=15',
    'MgefMaxHealth=30,MgefHealDrain=1');
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'RFAB_Disease_BoneBreakFever', 'BF',
    'MgefMaxStamina=15,MgefCarry=30',
    'MgefMaxStamina=30,MgefCarry=50,MgefSpeed=10');
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'RFAB_Disease_BrainRot', 'BRR',
    'MgefCostAlt=20,MgefCostConj=20,MgefCostDest=20,MgefCostIllu=20,MgefCostRest=20',
    'MgefCostAlt=40,MgefCostConj=40,MgefCostDest=40,MgefCostIllu=40,MgefCostRest=40,MgefMaxMagicka=20');
  BuildOneRfabWrapper(disTpl, 'RFAB.esp', 'DLC2DiseaseDroops', 'DR',
    'MgefWeapSpeed=10,MgefCastSpeed=20',
    'MgefWeapSpeed=15,MgefCastSpeed=30,MgefStamRegen=50');
end;

// True for MGEF EditorIDs from pre-balance-pass architectures (one MGEF per
// disease stage; hypothermia regen/hold effects) - all superseded by the shared
// penalty library + controller logic.
function IsStaleMgefEdid(e: string): Boolean;
begin
  Result :=
    (Pos(PFX + 'MgefColdCommon', e) = 1) or
    (Pos(PFX + 'MgefBrownRot',   e) = 1) or
    (Pos(PFX + 'MgefGutworm',    e) = 1) or
    (Pos(PFX + 'MgefGreenspore', e) = 1) or
    (Pos(PFX + 'MgefFoodPoison', e) = 1) or
    (Pos(PFX + 'MgefHypo',       e) = 1);
end;

// Remove those + the reverted _RSL_SpellHypo* paralysis spell, so they stop
// cluttering the active-effects list / xEdit and confusing debugging.
procedure PurgeStaleRecords;
var
  i: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
  ed: string;
begin
  Say('');
  Say('--- purge stale records ---');

  grp := GroupBySignature(tgt, 'MGEF');
  if Assigned(grp) then
    for i := Pred(ElementCount(grp)) downto 0 do begin
      r := ElementByIndex(grp, i);
      ed := EditorID(r);
      if IsStaleMgefEdid(ed) then begin
        Say('    removed MGEF ' + ed);
        Remove(r);
      end;
    end;

  grp := GroupBySignature(tgt, 'SPEL');
  if Assigned(grp) then
    for i := Pred(ElementCount(grp)) downto 0 do begin
      r := ElementByIndex(grp, i);
      ed := EditorID(r);
      if Pos(PFX + 'SpellHypo', ed) = 1 then begin
        Say('    removed SPEL ' + ed);
        Remove(r);
      end;
    end;
end;

// Scans for cure-disease MGEFs and records them (as "hex6=filename") in the
// global `cureForms`. WriteFormsScript turns that into _RSL_Forms.IsCureEffect,
// which resolves each via GetFormFromFile - no master of ours needed, unlike a
// FLST. Matched by "Cure Disease" archetype, a cure-disease EditorID, or the
// display name (this pack is Russian - both vanilla 000FBFF5 and RFAB's
// 070E463F are "Исцеление болезней"; the second has a non-standard archetype).
procedure ScanCureEffects;
var
  i, k, found: Integer;
  f  : IwbFile;
  g  : IwbGroupRecord;
  r  : IwbMainRecord;
  eid, arch, full, hex6, fn: string;
  names: TStringList;
begin
  Say('');
  Say('--- cure-disease effects ---');
  cureForms.Clear;

  names := TStringList.Create;
  names.Add('Skyrim.esm');
  names.Add('Update.esm');
  names.Add('Dawnguard.esm');
  names.Add('Requiem.esp');
  names.Add('RFAB.esp');   // merged mega-plugin (Requiem + DLC)
  try
    found := 0;
    for k := 0 to Pred(names.Count) do begin
      f := FileByName(names[k]);
      if not Assigned(f) then Continue;
      g := GroupBySignature(f, 'MGEF');
      if not Assigned(g) then Continue;
      for i := 0 to Pred(ElementCount(g)) do begin
        r := ElementByIndex(g, i);
        eid  := LowerCase(EditorID(r));
        arch := GetElementEditValues(r, 'Magic Effect Data\DATA\Archtype');
        full := GetElementEditValues(r, 'FULL');
        if SameText(arch, 'Cure Disease')
           or (Pos('curedisease', eid) > 0)
           or (Pos('cure_disease', eid) > 0)
           or ((Pos('cure', eid) > 0) and (Pos('disease', eid) > 0))
           or (Pos('сцеление болезн', full) > 0) then begin
          hex6 := IntToHex(GetLoadOrderFormID(MasterOrSelf(r)) and $00FFFFFF, 6);
          fn   := GetFileName(GetFile(MasterOrSelf(r)));
          if cureForms.IndexOf(hex6 + '=' + fn) < 0 then begin
            cureForms.Add(hex6 + '=' + fn);
            Inc(found);
            Say('    + [' + hex6 + '] ' + EditorID(r) + '  "' + full + '"  (' + fn + ')');
          end;
        end;
      end;
    end;
    if found = 0 then
      Say('  none found - potion/spell cure will not be detected, warmth-decay still works');
    Say('  cure effects: ' + IntToStr(found));
  finally
    names.Free;
  end;
end;

// VMAD - bind a script to a record. Only the flat VMAD\Scripts form is used;
// nested VMAD\Aliases is never used (see BuildMonitorAndQuest).

function AttachScript(rec: IwbMainRecord; scriptName: string): Boolean;
var
  vmad, scripts, scr: IInterface;
  i: Integer;
begin
  Result := False;

  vmad := ElementByPath(rec, 'VMAD');
  if not Assigned(vmad) then
    vmad := Add(rec, 'VMAD', True);
  if not Assigned(vmad) then begin
    Problem('VMAD not created in ' + EditorID(rec));
    Exit;
  end;

  PutNative(vmad, 'Version', 5);
  PutNative(vmad, 'Object Format', 2);

  scripts := ElementByPath(vmad, 'Scripts');
  if not Assigned(scripts) then
    scripts := Add(vmad, 'Scripts', True);
  if not Assigned(scripts) then begin
    Problem('no Scripts container in VMAD ' + EditorID(rec));
    Exit;
  end;

  // A rerun must not bind the script twice.
  for i := 0 to Pred(ElementCount(scripts)) do
    if SameText(GetElementEditValues(ElementByIndex(scripts, i), 'scriptName'), scriptName) then begin
      Say('  script ' + scriptName + ' already bound to ' + EditorID(rec));
      Result := True;
      Exit;
    end;

  scr := ElementAssign(scripts, HighInteger, nil, False);
  if not Assigned(scr) then begin
    Problem('script not added to VMAD ' + EditorID(rec));
    Exit;
  end;

  PutEdit(scr, 'scriptName', scriptName);
  PutNative(scr, 'Flags', 0);          // 0 = Local

  Say('  bound ' + scriptName + ' -> ' + EditorID(rec));
  Result := True;
end;

// Monitor ability + loader quest. All logic lives on an ActiveMagicEffect,
// not a ReferenceAlias: ActiveMagicEffect.psc gives exactly the events needed
// (OnObjectEquipped, OnSleepStart/Stop, OnPlayerLoadGame,
// OnVampirismStateChanged, RegisterForSingleUpdate, GetTargetActor) and
// avoids the fragile nested VMAD\Aliases. A quest is still needed to hand the
// player the ability, but it shrinks to a bare loader.

function FindScriptArchetypeTemplate: IwbMainRecord;
var
  src : IwbFile;
  grp : IwbGroupRecord;
  r   : IwbMainRecord;
  i   : Integer;
  arch, cast, deliv: string;
begin
  Result := nil;

  src := FileByName('Skyrim.esm');
  if not Assigned(src) then Exit;

  grp := GroupBySignature(src, 'MGEF');
  if not Assigned(grp) then Exit;

  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);

    arch  := GetElementEditValues(r, 'Magic Effect Data\DATA\Archtype');
    cast  := GetElementEditValues(r, 'Magic Effect Data\DATA\Casting Type');
    deliv := GetElementEditValues(r, 'Magic Effect Data\DATA\Delivery');

    if SameText(arch, 'Script')
       and SameText(cast, 'Constant Effect')
       and SameText(deliv, 'Self') then begin
      Result := r;
      Say('  monitor MGEF template: ' + EditorID(r) + '  [' + arch + ' / ' + cast + ' / ' + deliv + ']');
      Exit;
    end;
  end;

  Problem('no vanilla MGEF (Script / Constant Effect / Self)');
end;

// Single-effect ability (AddAbility builds three).
function AddAbility1(tpl: IwbMainRecord; edid: string; fullName: string;
                     mgef: IwbMainRecord): IwbMainRecord;
var
  effects, e: IInterface;
begin
  Result := RecordByEDID(tgt, 'SPEL', edid);
  if Assigned(Result) then begin
    ScrubTemplate(Result);
    Inc(reused);
    Remember(edid, Result);
    Exit;
  end;

  if not Assigned(mgef) then begin
    Problem('нет эффекта для ' + edid);
    Exit;
  end;

  Result := wbCopyElementToFile(tpl, tgt, True, True);
  if not Assigned(Result) then begin
    Problem('не скопировался SPEL ' + edid);
    Exit;
  end;

  ScrubTemplate(Result);

  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'FULL', fullName);

  effects := ElementByName(Result, 'Effects');
  if not Assigned(effects) then begin
    Problem('нет контейнера Effects в ' + edid);
    Exit;
  end;

  while ElementCount(effects) > 0 do
    RemoveByIndex(effects, 0, True);

  e := ElementAssign(effects, HighInteger, nil, False);
  if not Assigned(e) then begin
    Problem('effect not added to ' + edid);
    Exit;
  end;
  PutNative(e, 'EFID', GetLoadOrderFormID(mgef));
  PutNative(e, 'EFIT\Magnitude', 0.0);
  PutNative(e, 'EFIT\Area',      0);
  PutNative(e, 'EFIT\Duration',  0);

  Remember(edid, Result);
  Inc(madeNew);
end;

procedure BuildMonitorAndQuest;
var
  mgefTpl, spelTpl: IwbMainRecord;
  mon, qst        : IwbMainRecord;
  grp             : IwbGroupRecord;
begin
  Say('');
  Say('--- monitor MGEF, ability, loader quest ---');

  mgefTpl := FindScriptArchetypeTemplate;
  spelTpl := FindAbilityTemplate;

  if not Assigned(mgefTpl) or not Assigned(spelTpl) then begin
    Say('  no templates - skipping monitor');
    Exit;
  end;

  // effect that carries the logic
  mon := RecordByEDID(tgt, 'MGEF', PFX + 'MgefMonitor');
  if Assigned(mon) then begin
    ScrubTemplate(mon);
    Inc(reused);
    Remember(PFX + 'MgefMonitor', mon);
  end else begin
    mon := wbCopyElementToFile(mgefTpl, tgt, True, True);
    if not Assigned(mon) then begin
      Problem('monitor MGEF not copied');
      Exit;
    end;
    ScrubTemplate(mon);
    PutEdit(mon, 'EDID', PFX + 'MgefMonitor');
    PutEdit(mon, 'FULL', 'RSL Monitor');
    Remember(PFX + 'MgefMonitor', mon);
    Inc(madeNew);
  end;

  HideInUI(mon);
  AttachScript(mon, '_RSL_Controller');

  // ability that carries this effect
  AddAbility1(spelTpl, PFX + 'AbMonitor', 'RSL Monitor', mon);

  // loader quest
  qst := RecordByEDID(tgt, 'QUST', PFX + 'QstController');
  if Assigned(qst) then begin
    Inc(reused);
    Remember(PFX + 'QstController', qst);
  end else begin
    grp := EnsureGroup('QUST');
    if not Assigned(grp) then Exit;

    qst := Add(grp, 'QUST', True);
    if not Assigned(qst) then begin
      Problem('QUST not created');
      Exit;
    end;
    PutEdit(qst, 'EDID', PFX + 'QstController');

    // Start Game Enabled = bit 0. Without it the quest never starts.
    PutNative(qst, 'DNAM\Flags', 1);
    PutNative(qst, 'DNAM\Priority', 0);

    Remember(PFX + 'QstController', qst);
    Inc(madeNew);
  end;

  AttachScript(qst, '_RSL_Boot');
end;

// MCM quest. Without it the menu never appears, whatever config.json says.
// MCM Helper sits on top of SkyUI's MCM: the mod entry is a quest with a
// script extending MCM_ConfigBase; config.json alone registers nothing, it
// only describes the layout the base class reads in LoadConfig(). Every
// working MCM in the pack ships an _MCM.pex. _RSL_MCM itself is empty.

procedure BuildMcmQuest;
var
  qst: IwbMainRecord;
  grp: IwbGroupRecord;
begin
  Say('');
  Say('--- MCM quest ---');

  qst := RecordByEDID(tgt, 'QUST', PFX + 'QstMCM');
  if Assigned(qst) then begin
    Inc(reused);
    Remember(PFX + 'QstMCM', qst);
  end else begin
    grp := EnsureGroup('QUST');
    if not Assigned(grp) then Exit;

    qst := Add(grp, 'QUST', True);
    if not Assigned(qst) then begin
      Problem('MCM QUST not created');
      Exit;
    end;
    PutEdit(qst, 'EDID', PFX + 'QstMCM');
    PutNative(qst, 'DNAM\Flags', 1);      // Start Game Enabled
    PutNative(qst, 'DNAM\Priority', 0);

    Remember(PFX + 'QstMCM', qst);
    Inc(madeNew);
  end;

  AttachScript(qst, '_RSL_MCM');
end;

// HUD widget quest. _RSL_HUDWidget extends SKI_WidgetBase; SkyUI loads and
// positions the .swf, _RSL_Controller feeds values via UI.Invoke*. Like MCM,
// registration is lost on save load - _RSL_Controller re-kicks it from
// OnPlayerLoadGame.
procedure BuildWidgetQuest;
var
  qst: IwbMainRecord;
  grp: IwbGroupRecord;
begin
  Say('');
  Say('--- HUD widget quest ---');

  qst := RecordByEDID(tgt, 'QUST', PFX + 'QstWidget');
  if Assigned(qst) then begin
    Inc(reused);
    Remember(PFX + 'QstWidget', qst);
  end else begin
    grp := EnsureGroup('QUST');
    if not Assigned(grp) then Exit;

    qst := Add(grp, 'QUST', True);
    if not Assigned(qst) then begin
      Problem('widget QUST not created');
      Exit;
    end;
    PutEdit(qst, 'EDID', PFX + 'QstWidget');
    PutNative(qst, 'DNAM\Flags', 1);      // Start Game Enabled
    PutNative(qst, 'DNAM\Priority', 0);

    Remember(PFX + 'QstWidget', qst);
    Inc(madeNew);
  end;

  AttachScript(qst, '_RSL_HUDWidget');
end;

// Emit _RSL_Forms.psc. Forms are resolved via GetFormFromFile rather than
// VMAD properties: that keeps VMAD trivial (script name, zero properties) and
// the formIDs come from the same run that created the records.

procedure EmitFormGetter(sl: TStringList; papyrusType: string; fname: string; edid: string);
begin
  sl.Add(papyrusType + ' Function ' + fname + '() global');
  sl.Add('    return Game.GetFormFromFile(0x00' + RecalledHex(edid) + ', "' + PLUGIN_NAME + '") as ' + papyrusType);
  sl.Add('EndFunction');
  sl.Add('');
end;

procedure EmitGlobalGetter(sl: TStringList; shortName: string);
begin
  EmitFormGetter(sl, 'GlobalVariable', shortName, PFX + shortName);
end;

// 3 stage SPEL + 4 MESG getters for one from-scratch disease (see
// BuildScratchDisease). fname == EDID stem, e.g. 'BrownRot'.
procedure EmitScratchDiseaseGetters(sl: TStringList; key: string);
begin
  EmitFormGetter(sl, 'Spell',   'Disease' + key + '1', PFX + 'Disease' + key + '1');
  EmitFormGetter(sl, 'Spell',   'Disease' + key + '2', PFX + 'Disease' + key + '2');
  EmitFormGetter(sl, 'Spell',   'Disease' + key + '3', PFX + 'Disease' + key + '3');
  EmitFormGetter(sl, 'Message', 'Msg' + key + '1',     PFX + 'Msg' + key + '1');
  EmitFormGetter(sl, 'Message', 'Msg' + key + '2',     PFX + 'Msg' + key + '2');
  EmitFormGetter(sl, 'Message', 'Msg' + key + '3',     PFX + 'Msg' + key + '3');
  EmitFormGetter(sl, 'Message', 'Msg' + key + 'Cured', PFX + 'Msg' + key + 'Cured');
end;

// RFAB wrapper stages 2/3 + their messages. Stage 1 is RFAB's own record,
// emitted separately as a RfabDz<key> vanilla getter.
procedure EmitRfabWrapperGetters(sl: TStringList; key: string);
begin
  EmitFormGetter(sl, 'Spell',   'Dz' + key + '2',       PFX + 'Dz' + key + '2');
  EmitFormGetter(sl, 'Spell',   'Dz' + key + '3',       PFX + 'Dz' + key + '3');
  EmitFormGetter(sl, 'Message', 'MsgDz' + key + '2',    PFX + 'MsgDz' + key + '2');
  EmitFormGetter(sl, 'Message', 'MsgDz' + key + '3',    PFX + 'MsgDz' + key + '3');
  EmitFormGetter(sl, 'Message', 'MsgDz' + key + 'Cured', PFX + 'MsgDz' + key + 'Cured');
end;

// Getter for a vanilla form by raw formID (holds, visual, diseases).
procedure EmitVanillaGetter(sl: TStringList; papyrusType, fname, hex6, srcFile: string);
begin
  sl.Add(papyrusType + ' Function ' + fname + '() global');
  sl.Add('    return Game.GetFormFromFile(0x00' + hex6 + ', "' + srcFile + '") as ' + papyrusType);
  sl.Add('EndFunction');
  sl.Add('');
end;

// KYWD getter resolved by EditorID in Skyrim.esm (formID baked at generate time).
procedure EmitSkyrimKywd(sl: TStringList; fname, edid: string);
var
  kw  : IwbMainRecord;
  hex : string;
begin
  hex := '000000';
  kw := RecordByEDID(FileByName('Skyrim.esm'), 'KYWD', edid);
  if Assigned(kw) then
    hex := IntToHex(GetLoadOrderFormID(kw) and $00FFFFFF, 6)
  else
    Problem('KYWD ' + edid + ' not found in Skyrim.esm');
  sl.Add('Keyword Function ' + fname + '() global');
  sl.Add('    return Game.GetFormFromFile(0x00' + hex + ', "Skyrim.esm") as Keyword');
  sl.Add('EndFunction');
  sl.Add('');
end;

procedure WriteFormsScript;
var
  sl  : TStringList;
  path: string;
  undeadHex: string;
  src : IwbFile;
  kw  : IwbMainRecord;
  i, cp : Integer;
  cf  : string;
begin
  Say('');
  Say('--- emit _RSL_Forms.psc ---');

  // ActorTypeUndead lives in Skyrim.esm; pull its formID too, not hardcode.
  undeadHex := '000000';
  src := FileByName('Skyrim.esm');
  if Assigned(src) then begin
    kw := RecordByEDID(src, 'KYWD', 'ActorTypeUndead');
    if Assigned(kw) then
      undeadHex := IntToHex(GetLoadOrderFormID(kw) and $00FFFFFF, 6)
    else
      Problem('KYWD ActorTypeUndead not found in Skyrim.esm');
  end;

  sl := TStringList.Create;
  try
    // Header kept ASCII: TStringList writes single-byte, Cyrillic would break.
    sl.Add('Scriptname _RSL_Forms Hidden');
    sl.Add('{AUTO-GENERATED by SSEEdit_Scripts\RFAB_SurvivalLayer_01_Records.pas');
    sl.Add('');
    sl.Add(' DO NOT EDIT BY HAND -- the next generator run overwrites this file.');
    sl.Add('');
    sl.Add(' Forms are resolved via GetFormFromFile instead of VMAD properties,');
    sl.Add(' so formIDs come from the same run that created the records.}');
    sl.Add('');
    sl.Add('; --- settings ------------------------------------------------------');
    sl.Add('');

    EmitGlobalGetter(sl, 'ModEnabled');
    EmitGlobalGetter(sl, 'SleepGrace');
    EmitGlobalGetter(sl, 'SleepMax');
    EmitGlobalGetter(sl, 'SleepRestorePerHour');
    EmitGlobalGetter(sl, 'SleepMinHours');
    EmitGlobalGetter(sl, 'CombatFatigueMult');
    EmitGlobalGetter(sl, 'HungerGrace');
    EmitGlobalGetter(sl, 'HungerMax');
    EmitGlobalGetter(sl, 'HungerFoodPct');
    EmitGlobalGetter(sl, 'HungerSpecialFoodPct');
    EmitGlobalGetter(sl, 'RegionWinterhold');
    EmitGlobalGetter(sl, 'RegionPale');
    EmitGlobalGetter(sl, 'RegionEastmarch');
    EmitGlobalGetter(sl, 'RegionReach');
    EmitGlobalGetter(sl, 'RegionHjaalmarch');
    EmitGlobalGetter(sl, 'RegionHaafingar');
    EmitGlobalGetter(sl, 'RegionWhiterun');
    EmitGlobalGetter(sl, 'RegionFalkreath');
    EmitGlobalGetter(sl, 'RegionRift');
    EmitGlobalGetter(sl, 'RegionDefault');
    EmitGlobalGetter(sl, 'RegionSnowFloor');
    EmitGlobalGetter(sl, 'RegionAltitude');
    EmitGlobalGetter(sl, 'WeatherClear');
    EmitGlobalGetter(sl, 'WeatherCloudy');
    EmitGlobalGetter(sl, 'WeatherRain');
    EmitGlobalGetter(sl, 'WeatherSnow');
    EmitGlobalGetter(sl, 'NightMult');
    EmitGlobalGetter(sl, 'SwimMult');
    EmitGlobalGetter(sl, 'FireMult');
    EmitGlobalGetter(sl, 'SevInterior');
    EmitGlobalGetter(sl, 'SevColdInterior');
    EmitGlobalGetter(sl, 'AltitudeLow');
    EmitGlobalGetter(sl, 'AltitudeHigh');
    EmitGlobalGetter(sl, 'FireRadius');
    EmitGlobalGetter(sl, 'ColdRate');
    EmitGlobalGetter(sl, 'WarmthPerSlot');
    EmitGlobalGetter(sl, 'ResistWeight');
    EmitGlobalGetter(sl, 'DryMinutes');
    EmitGlobalGetter(sl, 'FrostHitCold');
    EmitGlobalGetter(sl, 'FireHitWarm');
    EmitGlobalGetter(sl, 'ElemLesionEnabled');
    EmitGlobalGetter(sl, 'ElemLesionColdThr');
    EmitGlobalGetter(sl, 'ElemLesionHypoChance');
    EmitGlobalGetter(sl, 'ElemLesionHitP');
    EmitGlobalGetter(sl, 'ElemLesionContractP');
    EmitGlobalGetter(sl, 'ElemLesionBandageP');
    EmitGlobalGetter(sl, 'PenaltyPrimary');
    EmitGlobalGetter(sl, 'PenaltyCross');
    EmitGlobalGetter(sl, 'PenaltyCap');
    EmitGlobalGetter(sl, 'TierStep');
    EmitGlobalGetter(sl, 'ColdGrace');
    EmitGlobalGetter(sl, 'WarmupMult');
    EmitGlobalGetter(sl, 'HudWidget');
    EmitGlobalGetter(sl, 'HudColor');
    EmitGlobalGetter(sl, 'HudWidgetAutoHide');
    EmitGlobalGetter(sl, 'HudWidgetX');
    EmitGlobalGetter(sl, 'HudWidgetY');
    EmitGlobalGetter(sl, 'HudWidgetScale');
    EmitGlobalGetter(sl, 'HudWidgetAlpha');
    EmitGlobalGetter(sl, 'HudWidgetHAnchor');
    EmitGlobalGetter(sl, 'HudWidgetVAnchor');
    EmitGlobalGetter(sl, 'PollInterval');
    EmitGlobalGetter(sl, 'DebugLog');

    // v2
    EmitGlobalGetter(sl, 'PenaltySpeed');
    EmitGlobalGetter(sl, 'SpeedCap');
    EmitGlobalGetter(sl, 'ColdVisualShader');
    EmitGlobalGetter(sl, 'ColdVisualThreshold');
    EmitGlobalGetter(sl, 'DiseaseEnabled');
    EmitGlobalGetter(sl, 'DiseaseProgressHours');
    EmitGlobalGetter(sl, 'DiseaseDecayHours');
    EmitGlobalGetter(sl, 'DiseaseHitChance');
    EmitGlobalGetter(sl, 'FoodPoisonChance');
    EmitGlobalGetter(sl, 'RfabDzEnabled');
    EmitGlobalGetter(sl, 'HypothermiaEnabled');
    EmitGlobalGetter(sl, 'HypothermiaThreshold');
    EmitGlobalGetter(sl, 'HypothermiaRecoverThr');
    EmitGlobalGetter(sl, 'HypothermiaWorsenHours');
    EmitGlobalGetter(sl, 'HypothermiaRecoverHours');
    EmitGlobalGetter(sl, 'HypothermiaDrainPerSec');
    EmitGlobalGetter(sl, 'HypothermiaDrainRamp');
    EmitGlobalGetter(sl, 'ColdColdThreshold');
    EmitGlobalGetter(sl, 'ColdColdChanceMin');
    EmitGlobalGetter(sl, 'ColdColdChanceMax');
    EmitGlobalGetter(sl, 'ColdColdChanceMaxAt');

    sl.Add('; --- abilities -----------------------------------------------------');
    sl.Add('');
    EmitFormGetter(sl, 'Spell', 'AbSleep',   PFX + 'AbSleep');
    EmitFormGetter(sl, 'Spell', 'AbHunger',  PFX + 'AbHunger');
    EmitFormGetter(sl, 'Spell', 'AbCold',    PFX + 'AbCold');
    EmitFormGetter(sl, 'Spell', 'AbMonitor', PFX + 'AbMonitor');

    sl.Add('; --- misc ----------------------------------------------------------');
    sl.Add('');
    EmitFormGetter(sl, 'FormList', 'FireSources',   PFX + 'FireSources');
    EmitFormGetter(sl, 'FormList', 'ColdInteriors', PFX + 'ColdInteriors');

    // Controller re-kicks OnGameReload on the MCM quest after a save load,
    // else the SKICP_configManagerReady subscription is lost for good.
    EmitFormGetter(sl, 'Quest', 'QstMCM', PFX + 'QstMCM');
    EmitFormGetter(sl, 'Quest', 'QstWidget', PFX + 'QstWidget');

    sl.Add('; --- vanilla forms -----------------------------------------------');
    sl.Add('');
    // hold locations (Skyrim.esm)
    EmitVanillaGetter(sl, 'Location', 'LocWinterhold', '01676B', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocPale',       '01676D', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocEastmarch',  '01676A', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocRift',       '01676C', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocFalkreath',  '01676F', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocWhiterun',   '016772', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocHaafingar',  '016770', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocHjaalmarch', '01676E', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Location', 'LocReach',      '016769', 'Skyrim.esm');
    // cold visual: FrostIceFormFXShader (character ice shader)
    EmitVanillaGetter(sl, 'EffectShader', 'FxColdShader', '0DC20D', 'Skyrim.esm');
    // OnHit disease carriers (Skyrim.esm)
    EmitVanillaGetter(sl, 'Race',    'RaceDraugr',        '000D53', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Race',    'RaceSlaughterfish', '013203', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Keyword', 'KwActorTypeTroll',  '0F5D16', 'Skyrim.esm');
    // food classification keywords (RFAB.esp, master-free)
    EmitVanillaGetter(sl, 'Keyword', 'KwRawFood',       '0CD63E', 'RFAB.esp');
    EmitVanillaGetter(sl, 'Keyword', 'KwStrongStomach', '4CF31E', 'RFAB.esp');
    EmitVanillaGetter(sl, 'Keyword', 'KwSpecialFood',   '0CD63D', 'RFAB.esp');
    EmitVanillaGetter(sl, 'Keyword', 'KwSpecialDrink',  '0CE2AD', 'RFAB.esp');

    sl.Add('Keyword Function ActorTypeUndead() global');
    sl.Add('    return Game.GetFormFromFile(0x00' + undeadHex + ', "Skyrim.esm") as Keyword');
    sl.Add('EndFunction');
    sl.Add('');

    // frost/fire hit -> cold-bar nudge; all three -> elemental-lesion P damage
    EmitSkyrimKywd(sl, 'KwMagicDamageFrost', 'MagicDamageFrost');
    EmitSkyrimKywd(sl, 'KwMagicDamageFire',  'MagicDamageFire');
    EmitSkyrimKywd(sl, 'KwMagicDamageShock', 'MagicDamageShock');

    sl.Add('; --- diseases: common cold --------------------------------------');
    sl.Add('');
    EmitFormGetter(sl, 'Spell', 'DiseaseColdCommon1', PFX + 'DiseaseColdCommon1');
    EmitFormGetter(sl, 'Spell', 'DiseaseColdCommon2', PFX + 'DiseaseColdCommon2');
    EmitFormGetter(sl, 'Spell', 'DiseaseColdCommon3', PFX + 'DiseaseColdCommon3');
    EmitFormGetter(sl, 'Message', 'MsgColdCommon1',     PFX + 'MsgColdCommon1');
    EmitFormGetter(sl, 'Message', 'MsgColdCommon2',     PFX + 'MsgColdCommon2');
    EmitFormGetter(sl, 'Message', 'MsgColdCommon3',     PFX + 'MsgColdCommon3');
    EmitFormGetter(sl, 'Message', 'MsgColdCommonCured', PFX + 'MsgColdCommonCured');

    sl.Add('; --- diseases: OnHit ------------------------------------------');
    sl.Add('');
    EmitScratchDiseaseGetters(sl, 'BrownRot');
    EmitScratchDiseaseGetters(sl, 'Gutworm');
    EmitScratchDiseaseGetters(sl, 'Greenspore');
    EmitScratchDiseaseGetters(sl, 'FoodPoison');
    EmitScratchDiseaseGetters(sl, 'ElemLesion');

    sl.Add('; --- RFAB disease wrappers (stage 1 = RFAB, stages 2/3 = ours) --');
    sl.Add('');
    // RFAB_Disease_* are RFAB's OVERRIDES of the vanilla Skyrim.esm Disease*
    // records (DiseaseAtaxia etc.); Droops is DLC2DiseaseDroops in Dragonborn.
    // GetFormFromFile needs the ORIGIN file, not RFAB.esp - it returns None for
    // a form that does not originate from the named plugin.
    EmitVanillaGetter(sl, 'Spell', 'RfabDzAT',  '0B877C', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Spell', 'RfabDzRJ',  '0B8782', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Spell', 'RfabDzWB',  '0B8783', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Spell', 'RfabDzRA',  '0B8781', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Spell', 'RfabDzBF',  '0B877E', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Spell', 'RfabDzBRR', '0B877F', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'Spell', 'RfabDzDR',  '0285C1', 'Dragonborn.esm');
    // Marker MGEF = the disease's first (unconditional) debuff effect. A disease
    // applied by a creature's RACE ATKD attack spell lands as active effects
    // WITHOUT the SPEL entering the spell list, so HasSpell misses it -
    // HasMagicEffect on this marker is the reliable contract detector.
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkAT',  '0CD9BD', 'RFAB.esp');
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkRJ',  '0B877A', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkWB',  '0B877B', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkRA',  '0B8779', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkBF',  '0B8776', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkBRR', '0B8777', 'Skyrim.esm');
    EmitVanillaGetter(sl, 'MagicEffect', 'RfabDzMarkDR',  '0285C0', 'Dragonborn.esm');
    EmitRfabWrapperGetters(sl, 'AT');
    EmitRfabWrapperGetters(sl, 'RJ');
    EmitRfabWrapperGetters(sl, 'WB');
    EmitRfabWrapperGetters(sl, 'RA');
    EmitRfabWrapperGetters(sl, 'BF');
    EmitRfabWrapperGetters(sl, 'BRR');
    EmitRfabWrapperGetters(sl, 'DR');

    sl.Add('; --- hypothermia ---------------------------------------------');
    sl.Add('');
    EmitFormGetter(sl, 'Spell',   'AbHypo1',      PFX + 'AbHypo1');
    EmitFormGetter(sl, 'Spell',   'AbHypo2',      PFX + 'AbHypo2');
    EmitFormGetter(sl, 'Spell',   'AbHypo3',      PFX + 'AbHypo3');
    EmitFormGetter(sl, 'Message', 'MsgHypo1',     PFX + 'MsgHypo1');
    EmitFormGetter(sl, 'Message', 'MsgHypo2',     PFX + 'MsgHypo2');
    EmitFormGetter(sl, 'Message', 'MsgHypo3',     PFX + 'MsgHypo3');
    EmitFormGetter(sl, 'Message', 'MsgHypoCured', PFX + 'MsgHypoCured');
    EmitFormGetter(sl, 'Message', 'MsgHypoNoRest', PFX + 'MsgHypoNoRest');

    sl.Add('; cure-disease effects (from ScanCureEffects). No FLST -> no master.');
    sl.Add('bool Function IsCureEffect(Form f) global');
    for i := 0 to Pred(cureForms.Count) do begin
      cf := cureForms[i];
      cp := Pos('=', cf);
      sl.Add('    if f == Game.GetFormFromFile(0x00' + Copy(cf, 1, cp - 1)
        + ', "' + Copy(cf, cp + 1, Length(cf)) + '")');
      sl.Add('        return true');
      sl.Add('    EndIf');
    end;
    sl.Add('    return false');
    sl.Add('EndFunction');
    sl.Add('');

    path := MOD_DIR + 'scripts\source\_RSL_Forms.psc';
    sl.SaveToFile(path);
    Say('  written: ' + path);
    Say('  REBUILD Papyrus after this: _build.bat');
  finally
    sl.Free;
  end;
end;

// Emit _RSL_Balance.psc: ResetDefaults() applies every GLOB's plugin default.
// Single source of truth - the defaults live only in BuildGlobals; balDefaults
// captured each one via AddGlobal. Called by MigrateSettings + the MCM reset.
procedure WriteBalanceScript;
var
  sl  : TStringList;
  path, ln, edid, val: string;
  i, p: Integer;
begin
  Say('');
  Say('--- emit _RSL_Balance.psc ---');
  sl := TStringList.Create;
  try
    sl.Add('Scriptname _RSL_Balance Hidden');
    sl.Add('{AUTO-GENERATED by SSEEdit_Scripts\RFAB_SurvivalLayer_01_Records.pas');
    sl.Add('');
    sl.Add(' DO NOT EDIT BY HAND. Every GLOB default lives in BuildGlobals in the');
    sl.Add(' generator; this file just re-applies them (new game / settings migration).}');
    sl.Add('');
    sl.Add('Function ResetDefaults() global');
    for i := 0 to Pred(balDefaults.Count) do begin
      ln := balDefaults[i];
      p := Pos('=', ln);
      if p = 0 then Continue;
      edid := Copy(ln, 1, p - 1);
      val  := Copy(ln, p + 1, Length(ln));
      // _RSL_Forms getters use the un-prefixed name (ModEnabled, not _RSL_ModEnabled)
      if Copy(edid, 1, Length(PFX)) = PFX then
        edid := Copy(edid, Length(PFX) + 1, Length(edid));
      sl.Add('    _RSL_Forms.' + edid + '().SetValue(' + val + '.0)');
    end;
    sl.Add('EndFunction');
    sl.Add('');
    path := MOD_DIR + 'scripts\source\_RSL_Balance.psc';
    sl.SaveToFile(path);
    Say('  written: ' + path + ' (' + IntToStr(balDefaults.Count) + ' defaults)');
  finally
    sl.Free;
  end;
end;

// Emit config.json for MCM Helper. Labels are $_RSL_Xxx keys, not text
// (TStringList writes single-byte, Cyrillic in JSON would break); the real
// labels live in Interface\Translations\RFAB_SurvivalLayer_*.txt.

// TrimLastComma runs before each ']', so every Json* helper unconditionally
// appends a comma - no need to track who is last.
procedure TrimLastComma(sl: TStringList);
var i: Integer; ln: string;
begin
  i := sl.Count - 1;
  while (i >= 0) and (Trim(sl[i]) = '') do i := i - 1;
  if i < 0 then Exit;
  ln := sl[i];
  if (Length(ln) > 0) and (ln[Length(ln)] = ',') then
    sl[i] := Copy(ln, 1, Length(ln) - 1);
end;

procedure JsonSlider(sl: TStringList; edid: string; min: string; max: string; step: string);
begin
  sl.Add('        {');
  sl.Add('          "text": "$' + edid + '",');
  sl.Add('          "help": "$' + edid + '_help",');
  sl.Add('          "type": "slider",');
  sl.Add('          "valueOptions": {');
  sl.Add('            "min": ' + min + ',');
  sl.Add('            "max": ' + max + ',');
  sl.Add('            "step": ' + step + ',');
  sl.Add('            "sourceType": "GlobalValue",');
  sl.Add('            "sourceForm": "' + SourceForm(edid) + '"');
  sl.Add('          }');
  sl.Add('        },');
end;

procedure JsonToggle(sl: TStringList; edid: string);
begin
  sl.Add('        {');
  sl.Add('          "text": "$' + edid + '",');
  sl.Add('          "help": "$' + edid + '_help",');
  sl.Add('          "type": "toggle",');
  sl.Add('          "valueOptions": {');
  sl.Add('            "sourceType": "GlobalValue",');
  sl.Add('            "sourceForm": "' + SourceForm(edid) + '"');
  sl.Add('          }');
  sl.Add('        },');
end;

procedure JsonHeader(sl: TStringList; key: string);
begin
  sl.Add('        {');
  sl.Add('          "text": "$' + key + '",');
  sl.Add('          "type": "header"');
  sl.Add('        },');
end;

// Read-only text row (MCM Helper "text" type, no action) - one line of help.
procedure JsonInfo(sl: TStringList; key: string);
begin
  sl.Add('        {');
  sl.Add('          "text": "$' + key + '",');
  sl.Add('          "type": "text"');
  sl.Add('        },');
end;

procedure WriteMcmConfig;
var
  sl  : TStringList;
  path: string;
begin
  Say('');
  Say('--- emit config.json ---');

  sl := TStringList.Create;
  try
    sl.Add('{');
    sl.Add('  "modName": "RFAB_SurvivalLayer",');
    // Plain text, not a $_RSL_ModName key: configs register early, before
    // translations load, and an unresolved key in displayName silently keeps
    // the mod out of the menu. Every working config in the pack does this too.
    sl.Add('  "displayName": "RFAB Survival",');
    sl.Add('  "pages": [');

    // page: Help - read-only, condensed from README.md. Text keys $_RSL_Hlp*
    // live in Interface/Translations/RFAB_SurvivalLayer_{russian,english}.txt.
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageHelp",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HlpHOverview');
    JsonInfo(sl, '_RSL_Hlp01');  JsonInfo(sl, '_RSL_Hlp02');  JsonInfo(sl, '_RSL_Hlp03');
    JsonInfo(sl, '_RSL_Hlp04');  JsonInfo(sl, '_RSL_Hlp05');  JsonInfo(sl, '_RSL_Hlp06');
    JsonHeader(sl, '_RSL_HlpHNeeds');
    JsonInfo(sl, '_RSL_Hlp07');  JsonInfo(sl, '_RSL_Hlp08');  JsonInfo(sl, '_RSL_Hlp09');
    JsonInfo(sl, '_RSL_Hlp10');  JsonInfo(sl, '_RSL_Hlp11');  JsonInfo(sl, '_RSL_Hlp12');
    JsonHeader(sl, '_RSL_HlpHCold');
    JsonInfo(sl, '_RSL_Hlp13');  JsonInfo(sl, '_RSL_Hlp14');  JsonInfo(sl, '_RSL_Hlp15');
    JsonInfo(sl, '_RSL_Hlp16');  JsonInfo(sl, '_RSL_Hlp17');  JsonInfo(sl, '_RSL_Hlp18');
    JsonInfo(sl, '_RSL_Hlp19');  JsonInfo(sl, '_RSL_Hlp20');  JsonInfo(sl, '_RSL_Hlp21');
    JsonInfo(sl, '_RSL_Hlp22');
    JsonHeader(sl, '_RSL_HlpHDisease');
    JsonInfo(sl, '_RSL_Hlp23');  JsonInfo(sl, '_RSL_Hlp24');  JsonInfo(sl, '_RSL_Hlp25');
    JsonInfo(sl, '_RSL_Hlp26');  JsonInfo(sl, '_RSL_Hlp27');  JsonInfo(sl, '_RSL_Hlp28');
    JsonInfo(sl, '_RSL_Hlp29');
    JsonHeader(sl, '_RSL_HlpHHypo');
    JsonInfo(sl, '_RSL_Hlp30');  JsonInfo(sl, '_RSL_Hlp31');  JsonInfo(sl, '_RSL_Hlp32');
    JsonInfo(sl, '_RSL_Hlp33');
    JsonHeader(sl, '_RSL_HlpHTune');
    JsonInfo(sl, '_RSL_Hlp34');  JsonInfo(sl, '_RSL_Hlp35');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: HUD
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageHud",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HdrHudWidget');
    JsonToggle(sl, PFX + 'HudWidget');
    JsonToggle(sl, PFX + 'HudColor');
    JsonToggle(sl, PFX + 'HudWidgetAutoHide');
    JsonHeader(sl, '_RSL_HdrHudPos');
    JsonSlider(sl, PFX + 'HudWidgetX',      '0', '1280', '5');
    JsonSlider(sl, PFX + 'HudWidgetY',      '0', '720',  '5');
    JsonSlider(sl, PFX + 'HudWidgetScale',  '50', '200', '5');
    JsonSlider(sl, PFX + 'HudWidgetAlpha',  '0', '100',  '5');
    // Anchors 0/1/2 as sliders (0 left/top, 1 center, 2 right/bottom);
    // MCM Helper menu with GlobalValue is untested in this pack.
    JsonSlider(sl, PFX + 'HudWidgetHAnchor', '0', '2', '1');
    JsonSlider(sl, PFX + 'HudWidgetVAnchor', '0', '2', '1');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: Sleep
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageSleep",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HdrSleepCurve');
    JsonSlider(sl, PFX + 'SleepGrace',          '0',  '48',  '1');
    JsonSlider(sl, PFX + 'SleepMax',            '24', '168', '1');
    JsonHeader(sl, '_RSL_HdrSleepRecovery');
    JsonSlider(sl, PFX + 'SleepRestorePerHour', '1',  '24',  '1');
    JsonSlider(sl, PFX + 'SleepMinHours',       '0',  '8',   '1');
    JsonHeader(sl, '_RSL_HdrCombat');
    JsonSlider(sl, PFX + 'CombatFatigueMult',   '1',  '15',  '1');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: Hunger
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageHunger",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HdrHungerCurve');
    JsonSlider(sl, PFX + 'HungerGrace', '0',  '48',  '1');
    JsonSlider(sl, PFX + 'HungerMax',   '24', '168', '1');
    JsonHeader(sl, '_RSL_HdrHungerFood');
    JsonSlider(sl, PFX + 'HungerFoodPct',        '0', '200', '5');
    JsonSlider(sl, PFX + 'HungerSpecialFoodPct', '0', '300', '5');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: Cold
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageCold",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HdrColdModel');
    JsonSlider(sl, PFX + 'ColdRate',      '0.25', '3',  '0.25');  // overall rate multiplier
    JsonSlider(sl, PFX + 'ColdGrace',     '0',    '100', '5');
    JsonSlider(sl, PFX + 'WarmupMult',    '1',    '10',  '0.5');
    JsonSlider(sl, PFX + 'WarmthPerSlot', '0',   '30',  '1');
    JsonSlider(sl, PFX + 'ResistWeight',  '0',   '200', '5');   // % of FrostResist
    JsonSlider(sl, PFX + 'DryMinutes',    '0',   '30',  '1');
    JsonSlider(sl, PFX + 'FrostHitCold',  '0',   '20',  '1');
    JsonSlider(sl, PFX + 'FireHitWarm',   '0',   '20',  '1');
    JsonHeader(sl, '_RSL_HdrRegion');
    JsonSlider(sl, PFX + 'RegionWinterhold', '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionPale',       '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionEastmarch',  '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionReach',      '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionHjaalmarch', '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionHaafingar',  '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionWhiterun',   '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionFalkreath',  '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionRift',       '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionDefault',    '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionSnowFloor',  '0', '150', '5');
    JsonSlider(sl, PFX + 'RegionAltitude',   '0', '200', '5');
    JsonHeader(sl, '_RSL_HdrMult');
    JsonSlider(sl, PFX + 'WeatherClear',      '50',  '400', '5');
    JsonSlider(sl, PFX + 'WeatherCloudy',     '50',  '400', '5');
    JsonSlider(sl, PFX + 'WeatherRain',       '50',  '400', '5');
    JsonSlider(sl, PFX + 'WeatherSnow',       '50',  '400', '5');
    JsonSlider(sl, PFX + 'NightMult',         '100', '400', '5');
    JsonSlider(sl, PFX + 'SwimMult',          '100', '500', '5');
    JsonSlider(sl, PFX + 'FireMult',          '0',   '100', '5');
    JsonSlider(sl, PFX + 'SevInterior',       '0',   '150', '5');
    JsonSlider(sl, PFX + 'SevColdInterior',   '0',   '150', '5');
    JsonSlider(sl, PFX + 'AltitudeLow',       '0',   '25000', '500');
    JsonSlider(sl, PFX + 'AltitudeHigh',      '0',   '30000', '500');
    JsonSlider(sl, PFX + 'FireRadius',        '100', '2000',  '50');
    // visual
    JsonHeader(sl, '_RSL_HdrColdVisual');
    JsonToggle(sl, PFX + 'ColdVisualShader');
    JsonSlider(sl, PFX + 'ColdVisualThreshold', '0', '100', '5');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: Diseases
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageDisease",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HdrDisease');
    JsonToggle(sl, PFX + 'DiseaseEnabled');
    JsonSlider(sl, PFX + 'DiseaseProgressHours', '5', '120', '5');
    JsonSlider(sl, PFX + 'DiseaseDecayHours',    '5', '240', '5');
    JsonSlider(sl, PFX + 'DiseaseHitChance',     '0', '100', '5');
    JsonSlider(sl, PFX + 'FoodPoisonChance',     '0', '100', '5');
    JsonToggle(sl, PFX + 'RfabDzEnabled');
    JsonToggle(sl, PFX + 'HypothermiaEnabled');
    JsonSlider(sl, PFX + 'HypothermiaThreshold',    '0', '100', '5');
    JsonSlider(sl, PFX + 'HypothermiaWorsenHours',  '1', '24',  '1');
    JsonSlider(sl, PFX + 'HypothermiaRecoverHours', '1', '24',  '1');
    JsonSlider(sl, PFX + 'HypothermiaDrainPerSec',  '0', '10',  '1');
    JsonSlider(sl, PFX + 'HypothermiaDrainRamp',    '5', '120', '5');
    JsonSlider(sl, PFX + 'ColdColdThreshold',        '0', '100', '5');
    JsonSlider(sl, PFX + 'ColdColdChanceMin',    '0', '100', '5');
    JsonSlider(sl, PFX + 'ColdColdChanceMax',    '0', '100', '5');
    JsonSlider(sl, PFX + 'ColdColdChanceMaxAt',  '0', '100', '5');
    JsonToggle(sl, PFX + 'ElemLesionEnabled');
    JsonSlider(sl, PFX + 'ElemLesionHypoChance', '0', '100', '5');
    JsonSlider(sl, PFX + 'ElemLesionHitP',       '0', '20',  '1');
    sl.Add('        {');
    sl.Add('          "text": "$_RSL_BtnCureCold",');
    sl.Add('          "help": "$_RSL_BtnCureCold_help",');
    sl.Add('          "type": "text",');
    sl.Add('          "action": {');
    sl.Add('            "type": "CallGlobalFunction",');
    sl.Add('            "script": "_RSL_Controller",');
    sl.Add('            "function": "CureColdDisease"');
    sl.Add('          }');
    sl.Add('        },');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: Penalties
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PagePenalty",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonHeader(sl, '_RSL_HdrPenalty');
    JsonSlider(sl, PFX + 'PenaltyPrimary', '0', '100', '5');
    JsonSlider(sl, PFX + 'PenaltyCross',   '0', '100', '5');
    JsonSlider(sl, PFX + 'PenaltySpeed',   '0', '50',  '5');
    JsonSlider(sl, PFX + 'SpeedCap',       '0', '80',  '5');
    JsonSlider(sl, PFX + 'PenaltyCap',     '0', '95',  '5');
    JsonSlider(sl, PFX + 'TierStep',       '5', '25',  '5');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    },');

    // page: Debug - master switch + service controls + the reset button.
    sl.Add('    {');
    sl.Add('      "pageDisplayName": "$_RSL_PageDebug",');
    sl.Add('      "cursorFillMode": "topToBottom",');
    sl.Add('      "content": [');
    JsonToggle(sl, PFX + 'ModEnabled');
    JsonSlider(sl, PFX + 'PollInterval', '1', '30', '1');
    JsonToggle(sl, PFX + 'DebugLog');
    sl.Add('        {');
    sl.Add('          "text": "$_RSL_BtnReset",');
    sl.Add('          "help": "$_RSL_BtnReset_help",');
    sl.Add('          "type": "text",');
    sl.Add('          "action": {');
    sl.Add('            "type": "CallGlobalFunction",');
    sl.Add('            "script": "_RSL_Controller",');
    sl.Add('            "function": "ResetDefaults"');
    sl.Add('          }');
    sl.Add('        }');
    TrimLastComma(sl);
    sl.Add('      ]');
    sl.Add('    }');

    sl.Add('  ]');
    sl.Add('}');

    path := MOD_DIR + 'MCM\Config\RFAB_SurvivalLayer\config.json';
    sl.SaveToFile(path);
    Say('  записан: ' + path);
    Say('  ВНИМАНИЕ: кнопки "Вылечить простуду" (Болезни) и "Сброс настроек"');
    Say('  (Debug) зовут глобальные функции _RSL_Controller через');
    Say('  CallGlobalFunction. Если не сработают - сверить с документацией');
    Say('  MCM Helper и при необходимости перейти на CallFunction с квестом.');
  finally
    sl.Free;
  end;
end;

// ---------------------------------------------------------------------------

function Initialize: Integer;
begin
  Result   := 0;
  problems := 0;
  madeNew  := 0;
  reused   := 0;
  ids      := TStringList.Create;
  cureForms := TStringList.Create;
  balDefaults := TStringList.Create;

  Say('============================================================');
  Say(' RFAB Survival Layer -- генератор записей, часть 1');
  Say('============================================================');

  LoadStrings;

  tgt := EnsureTargetFile;
  if not Assigned(tgt) then begin
    Result := 1;
    Exit;
  end;

  BuildGlobals;
  BuildFireList;
  BuildColdInteriors;
  BuildEffectsAndSpells;
  BuildPenaltyLib;
  BuildDiseases;
  BuildRfabWrappers;
  BuildHypothermia;
  PurgeStaleRecords;
  ScanCureEffects;
  BuildMonitorAndQuest;
  BuildMcmQuest;
  BuildWidgetQuest;
  WriteFormsScript;
  WriteBalanceScript;
  WriteMcmConfig;

  Say('');
  Say('============================================================');
  Say(' Создано записей: ' + IntToStr(madeNew));
  Say(' Уже было (пропущено): ' + IntToStr(reused));
  if problems = 0 then
    Say(' Проблем нет.')
  else
    Say(' ПРОБЛЕМ: ' + IntToStr(problems) + ' -- читать лог выше.');
  Say('============================================================');
  Say('');
  Say('ДАЛЬШЕ:');
  Say('  1. Сохранить плагин.');
  Say('  2. Перенести его из Overwrite в mods\RFAB Survival Layer\.');
  Say('  3. Пересобрать Papyrus: _build.bat');
  Say('     (_RSL_Forms.psc только что перегенерирован -- без пересборки');
  Say('      скрипты будут смотреть на старые formID)');
  Say('  4. Включить мод и плагин в MO2, порядок -- ниже RFAB.esp.');
  Say('  5. ESL-флаг не ставить, пока всё не заработает (§11).');
end;

function Process(e: IInterface): Integer;
begin
  Result := 0;
end;

function Finalize: Integer;
begin
  if Assigned(ids) then
    ids.Free;
  if Assigned(cureForms) then
    cureForms.Free;
  if Assigned(balDefaults) then
    balDefaults.Free;
  if Assigned(strTbl) then
    strTbl.Free;
  Result := 0;
end;

end.
