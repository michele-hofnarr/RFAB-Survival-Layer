{
  RFAB Survival Layer - plugin generator, part 1: records.

  Creates in RFAB_SurvivalLayer.esp: 1 FLST (fire sources), 12 MGEF (Value
  Modifier on H/M/S/SpeedMult - axis penalties), 3 SPEL (axis abilities),
  1 Script-MGEF + 1 SPEL (monitor), 3 QUST (Start Game Enabled: controller,
  MCM, widget). Also emits native\src\Core\FormIDs.h so no formID is ever
  hand-copied between the plugin and the DLL.

  Papyrus is down to one script, _RSL_MCM, and it is empty: MCM Helper needs a
  quest with a script extending MCM_ConfigBase before it will register the menu
  at all. Everything else the mod does is in the native plugin, so nothing else
  is bound - and the bindings are actively DROPPED, because a plugin generated
  by an older run still carries them.

  Settings are NOT here. They live in an ini the native plugin reads directly
  (MCM\Config\RFAB_SurvivalLayer\settings.ini), and the menu that edits them
  is generated from the plugin's own Settings.h by tools/make_mcm.py. That is
  what removed the 109 GLOBs, _RSL_Balance.psc and the config.json emitter
  this file used to carry.

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
  FLAG_HOSTILE     = $00000001;

  // EQUP EitherHand (Skyrim.esm 00013F44). Every one of the 250 vanilla
  // Ability SPELs and all 13 Disease SPELs carry an ETYP, and 242 of the
  // abilities use this one. A SPEL with no ETYP is the shape AddSpell
  // silently refuses to instantiate - see NormalizeSpit.
  EQUP_EITHER_HAND = $00013F44;

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

// Is this key present at all? L() shouts and returns the key name when it is
// not, which is the right default for text that must exist - but a caller with
// a genuinely optional key needs to ask quietly first.
function HasStr(key: string): Boolean;
begin
  Result := strTbl.IndexOfName(key) >= 0;
end;

// Real line breaks, for a value that had to arrive on one line.
//
// The string file is key=value, one key per line, so a literal newline cannot
// be written in it. The source writes \n and this turns it into what the game
// wants - which for a HELP PAGE is a real CRLF, not the <br> that perk
// descriptions use. Measured on RFAB's own: HelpEnchantingLong carries ten
// pairs of #13#10 and not one <br>.
function Multiline(s: string): string;
begin
  Result := StringReplace(s, '\n', #13#10, [rfReplaceAll]);
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
  i  : Integer;
begin
  Result := False;
  if not Assigned(r) then Exit;
  fid := GetLoadOrderFormID(r);
  if (fid = 0) or ((fid and $00FFFFFF) = 0) then Exit;
  if FlstHasFid(items, fid) then Exit;
  // never add tgt as its own master (self-master -> a recursive / broken esp)
  if not SameText(GetFileName(GetFile(MasterOrSelf(r))), GetFileName(tgt)) then
    AddMasterIfMissing(tgt, GetFileName(GetFile(MasterOrSelf(r))));

  // A BLANK ENTRY IS FILLED, NOT APPENDED AFTER. EnsureFlstItems creates the
  // FormIDs container, and creating it already makes one entry pointing at
  // nothing - the same trap AddCond documents for conditions. Every list this
  // file rebuilt since v0.3.0 went out with a 00000000 in slot 0 because of
  // it, found by reading the saved plugin back.
  el := nil;
  i := 0;
  while (i < ElementCount(items)) and not Assigned(el) do begin
    if GetNativeValue(ElementByIndex(items, i)) = 0 then
      el := ElementByIndex(items, i);
    Inc(i);
  end;
  if not Assigned(el) then
    el := ElementAssign(items, HighInteger, nil, False);
  if not Assigned(el) then Exit;
  // Written as a REFERENCE, not as a number. GetLoadOrderFormID + SetNativeValue
  // is remapped to the file's own master list when the plugin is saved - except
  // it was not for the entry appended to HelpManualPC right after that same run
  // gave the file a new master: it saved as 16001007, the load order id, which
  // resolves to nothing. Name() is what xEdit's own scripts pass for a FormID
  // field ("Read Books Aloud" sets SDSC that way) and it is resolved on write.
  SetEditValue(el, Name(r));
  if GetNativeValue(el) = 0 then begin
    Problem('FlstAddRecord: ссылка не записалась: ' + Name(r));
    Exit;
  end;
  Result := True;
end;

// A finished list holds records and nothing else: every entry must resolve.
// Asserted rather than assumed, because the blank slot FlstAddRecord now
// fills went unnoticed for three releases.
procedure FlstRequireResolved(items: IInterface; edid: string);
var
  i: Integer;
begin
  if not Assigned(items) then Exit;
  for i := 0 to Pred(ElementCount(items)) do
    if not Assigned(LinksTo(ElementByIndex(items, i))) then
      Problem(edid + ': entry ' + IntToStr(i) + ' resolves to nothing ("'
        + GetEditValue(ElementByIndex(items, i)) + '")');
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

// Get-or-create an FLST by EditorID and hand back a FRESH, empty FormIDs
// container. Both our lists are rebuilt from scratch on every run (the
// EditorID masks that feed them change between runs), so the reset is shared.
function EnsureFlstItems(edid: string): IInterface;
var
  flst: IwbMainRecord;
  grp : IwbGroupRecord;
  old : IInterface;
begin
  Result := nil;
  flst := RecordByEDID(tgt, 'FLST', edid);
  if Assigned(flst) then
    Inc(reused)
  else begin
    grp := EnsureGroup('FLST');
    if not Assigned(grp) then Exit;
    flst := Add(grp, 'FLST', True);
    if not Assigned(flst) then begin
      Problem('FLST не создался: ' + edid);
      Exit;
    end;
    PutEdit(flst, 'EDID', edid);
    Inc(madeNew);
  end;
  Remember(edid, flst);

  old := ElementByName(flst, 'FormIDs');
  if Assigned(old) then Remove(old);
  Result := Add(flst, 'FormIDs', True);
  if not Assigned(Result) then
    Problem('FLST ' + edid + ': нет контейнера FormIDs');
end;

procedure BuildFireList;
var
  items: IInterface;
  sigs : TStringList;
  found: Integer;
begin
  Say('');
  Say('--- FLST: fire sources ---');

  items := EnsureFlstItems(PFX + 'FireSources');
  if not Assigned(items) then Exit;

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
    FlstRequireResolved(items, PFX + 'FireSources');
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
  items: IInterface;
  found: Integer;
begin
  Say('');
  Say('--- FLST: cold interiors ---');

  items := EnsureFlstItems(PFX + 'ColdInteriors');
  if not Assigned(items) then Exit;

  found := 0;
  FlstAddEdids(items, 'Skyrim.esm', 'LCTN',
    'BleakcoastCaveLocation BonechillPassageLocation ColdRockPassLocation ' +
    'DuskglowCreviceLocation FrostflowLighthouseLocation GreywaterGrottoLocation ' +
    'HaemarsShameLocation HobsFallCaveLocation SeptimusSignusOutpostLocation ' +
    'SightlessPitLocation SouthfringeSanctumLocation SteepfallBurrowLocation ' +
    'StillbornCaveLocation YngvildLocation AlftandLocation ForsakenCaveLocation ' +
    // Added by hand, from play rather than from CC's list: Uttering Hills Cave
    // is an ice cave the vanilla set does not name. Its location covers the
    // exterior cells as well, which costs nothing - the controller only asks
    // this question once it already knows the player is indoors.
    'UtteringHillsCampLocation', found);
  FlstAddEdids(items, 'Dawnguard.esm', 'LCTN', 'DLC1GlacialCreviceLocation', found);
  FlstAddEdids(items, 'Dragonborn.esm', 'LCTN',
    'DLC2AltarofThrondLocation DLC2BenkongerikeLocation DLC2BristlebackCaveLocation ' +
    'DLC2FrosselLocation DLC2GlacialCaveLocation', found);

  if found = 0 then
    Problem('cold-interiors list empty - masters not loaded?');
  FlstRequireResolved(items, PFX + 'ColdInteriors');
  Say('  items added: ' + IntToStr(found));
end;

// Strip inherited baggage from a record copied off a vanilla template.
// wbCopyElementToFile copies the whole record: VMAD (template scripts stay
// bound and RUN), DNAM (template description), KSIZ/KWDA (keywords like
// MagicAlchHarmful), MDOB. All removed right after the copy.

// --- conditions (CTDA) -----------------------------------------------------
//
// The comparison operator lives in the top three bits of the Type byte, so a
// value of 0 is "equal to" and 3 shifted up (3 * 32 = 96) is "greater than or
// equal to". Everything else in the byte is flags we do not use.
const
  CTDA_OP_EQ = 0;
  CTDA_OP_GE = 3;

  CTDA_FUNC_GETITEMCOUNT = 47;
  CTDA_FUNC_HASPERK      = 448;

// Point a form-reference field at a record in ANOTHER file.
//
// SetNativeValue writes a formID VERBATIM. GetLoadOrderFormID hands back the
// index the file has in the LOAD ORDER, and a saved plugin stores the index
// into its OWN master list - two different numbers whenever they are not the
// same file. Everything this script wrote before got away with it by accident:
//
//   Skyrim.esm   load-order index 0, our master index 0. Equal, so correct.
//   our own file load-order index 22, and the engine reads any index at or
//                above the master count as "this plugin", so 22 lands home.
//   RFAB.esp     load-order index 7, our master index 4. NOT equal, and 7 is
//                also >= our 5 masters - so it read as OUR file, formID
//                0703DFE8, which is nothing. xEdit showed it as
//                "<Error: Could not be resolved>" and the recipe made nothing.
//
// The EDIT value is the path that does the mapping, so that is what is written
// here - and then read back through LinksTo, because a silently wrong
// cross-file reference is exactly the bug this comment exists to describe.
function PutFormID(rec: IInterface; path: string; src: IwbMainRecord): Boolean;
var
  el : IInterface;
  got: IwbMainRecord;
begin
  Result := False;
  if not Assigned(src) then Exit;

  AddMasterIfMissing(tgt, GetFileName(GetFile(MasterOrSelf(src))));

  el := ElementByPath(rec, path);
  if not Assigned(el) then
    el := Add(rec, path, True);
  if not Assigned(el) then begin
    Problem('нет поля "' + path + '" в ' + Name(rec));
    Exit;
  end;

  try
    SetEditValue(el, IntToHex(GetLoadOrderFormID(src), 8));
  except
    on E: Exception do begin
      Problem('ссылка "' + path + '" в ' + Name(rec) + ': ' + E.Message);
      Exit;
    end;
  end;

  got := LinksTo(el);
  if Assigned(got) and SameText(EditorID(got), EditorID(src)) then begin
    Result := True;
    Exit;
  end;
  Problem('"' + path + '" в ' + Name(rec) + ' не разрешилось в ' + EditorID(src));
end;

// Write the condition's first parameter.
//
// It is NOT addressed by one path, because xEdit RENAMES it once the function
// is set: "Perk" for HasPerk, "Inventory Object" for GetItemCount, and plain
// "Parameter #1" while the function is still unknown. A single hardcoded path
// therefore works for one function and silently writes nothing for the next -
// which is a recipe that is always available, or never. So: try the names, and
// read the value back. No read-back, no recipe.
function PutCondParam(c: IInterface; src: IwbMainRecord): Boolean;
var
  names: TStringList;
  i    : Integer;
begin
  Result := False;
  names := TStringList.Create;
  try
    names.Add('CTDA\Perk');
    names.Add('CTDA\Inventory Object');
    names.Add('CTDA\Parameter #1');
    for i := 0 to Pred(names.Count) do begin
      if not Assigned(ElementByPath(c, names[i])) then Continue;
      // PutFormID does the master mapping AND the read-back; a name that is
      // not this function's takes the same route and simply fails it.
      if PutFormID(c, names[i], src) then begin
        Result := True;
        Exit;
      end;
    end;
  finally
    names.Free;
  end;
end;

// One condition on anything that has a Conditions container.
//
// CREATING THE CONTAINER ALREADY MAKES AN ENTRY, and that entry is blank:
// function 0, comparison 0, no parameter. Appending a second one after it
// leaves the blank in the record, and it is written out with everything else -
// the bedroll recipe went out with "GetWantBlocking == 0" standing in front of
// its perk check because of exactly this. Nothing noticed, because every other
// caller works on a record whose template brought a container along.
//
// So the entry the container came with is the one that gets filled in. Read
// back off the saved plugin rather than reasoned about: RecipeWater has its
// two conditions and nothing else, RecipeBedroll had three where two were
// asked for.
procedure AddCond(rec: IInterface; funcIdx, op: Integer; cmp: Variant; param: IwbMainRecord);
var
  conds, c: IInterface;
  fresh: Boolean;
begin
  fresh := False;
  conds := ElementByName(rec, 'Conditions');
  if not Assigned(conds) then begin
    conds := Add(rec, 'Conditions', True);
    fresh := True;
  end;
  if not Assigned(conds) then begin
    Problem('нет контейнера Conditions в ' + Name(rec));
    Exit;
  end;

  if fresh and (ElementCount(conds) = 1) then
    c := ElementByIndex(conds, 0)
  else
    c := ElementAssign(conds, HighInteger, nil, False);
  if not Assigned(c) then begin
    Problem('условие не создалось в ' + Name(rec));
    Exit;
  end;

  // Function first: the parameter's name follows from it.
  PutNative(c, 'CTDA\Function', funcIdx);
  PutNative(c, 'CTDA\Type', op * 32);
  PutNative(c, 'CTDA\Comparison Value', cmp);
  PutNative(c, 'CTDA\Run On', 0);            // Subject = the player
  if not PutCondParam(c, param) then
    Problem('параметр условия не записался (функция ' + IntToStr(funcIdx)
          + ') в ' + Name(rec));
end;

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

// EVERY CONDITION, not the first one, and through the container.
//
// DropElement(rec, 'CTDA') was meant to do this and never removed a thing:
// xEdit keeps a magic effect's conditions inside a "Conditions" array, so
// ElementBySignature at record level finds nothing and the Say never fires.
// Twenty of our records shipped carrying a condition because of it - four
// library effects copied from RFAB's Peryite records, the campfire effect, and
// every face cloned off one of those.
//
// What the condition is does not matter here. It is RFAB's, it gates RFAB's own
// disease debuffs against RFAB's own boon, and a penalty this mod applies has no
// business answering to it.
// BOTH WAYS ROUND, and it says when neither worked.
//
// Whether a record's conditions answer to ElementByName('Conditions') or only
// to ElementBySignature depends on the definition, and guessing wrong is how
// the old line failed: it found nothing, removed nothing, and printed nothing.
// A drop that cannot be seen to have happened is the bug, not the mechanism.
procedure DropConditions(rec: IwbMainRecord);
var
  cond, el: IInterface;
  n       : Integer;
begin
  if not Assigned(rec) then Exit;

  n := 0;

  // Signature first, which is where DumpMgefGate below found them.
  el := ElementBySignature(rec, 'CTDA');
  while Assigned(el) and (n < 64) do begin
    Remove(el);
    Inc(n);
    el := ElementBySignature(rec, 'CTDA');
  end;

  // ...and the container, for whatever that did not account for.
  cond := ElementByName(rec, 'Conditions');
  if Assigned(cond) then
    while (ElementCount(cond) > 0) and (n < 64) do begin
      RemoveByIndex(cond, 0, True);
      Inc(n);
    end;

  if n >= 64 then
    Problem('DropConditions: ' + EditorID(rec) + ' would not let go - stopped at 64')
  else if n > 0 then
    Say('    dropped ' + IntToStr(n) + ' condition(s) from ' + EditorID(rec));
end;

// A PLAIN VALUE MODIFIER, ALWAYS.
//
// Peak and Dual value modifiers DO NOT STACK: the engine keeps one per actor
// value, and a second is never instantiated at all - no effect, no penalty, and
// nothing in the active-effects list to show the illness is there. Proven in
// play and by the records: a tissue stress at stage 1 had two of its three
// effects missing, one displaced by RFAB's permanent attack-speed bonus and the
// other by our own green spore, while three plain ValueModifiers on the same two
// actor values ran side by side - two of them the same record twice over.
//
// Nothing chose those archetypes. They are whatever the vanilla or RFAB effect
// each library entry was copied from happened to be, and the twenty entries that
// came out non-stacking collided with RFAB on six actor values and with EACH
// OTHER on six more. Half the illnesses were applying nothing.
//
// The Script archetype is left alone: the campfire effect and the monitor are
// not value modifiers and have no actor value to hold.
//
// COST, stated rather than buried: _RSL_MgefWeapSpeed was the one Dual entry,
// and dual is how one effect reaches both hands - actor value 85 and 132. As a
// plain modifier it reaches the right hand only, so a dual-wielding character's
// off hand is no longer slowed by tissue stress or hypothermia. Covering it
// again takes a second library entry, which needs a name.
procedure ForceValueModifier(rec: IwbMainRecord);
var
  arch, av: string;
begin
  if not Assigned(rec) then Exit;

  arch := GetElementEditValues(rec, 'Magic Effect Data\DATA\Archtype');
  if arch = '' then begin
    // The DATA tree is not open. Every caller writes Casting Type and Delivery
    // first precisely so that it is, so this means the record is not shaped the
    // way this file assumes - and saying nothing is how the last one of these
    // went unnoticed for twenty records.
    Problem('ForceValueModifier: no Archtype on ' + EditorID(rec));
    Exit;
  end;
  if SameText(arch, 'Value Modifier') or SameText(arch, 'Script') then Exit;

  // THE ACTOR VALUE IS TAKEN FIRST AND PUT BACK AFTER, because writing the
  // archetype clears it.
  //
  // This file already warns about it twice - "an Actor Value clobbered to
  // Aggression", and the note on Casting Type / Delivery being safe because
  // they "CANNOT clobber an enum (unlike blindly echoing Actor Value)" - and
  // the first version of this procedure walked straight into it anyway. It
  // converted 48 records and left every one of them modifying actor value
  // None, which is a value modifier that modifies nothing: the same illness
  // applying nothing as before, by a different route, and check_effects.py
  // passed it.
  av := GetElementEditValues(rec, 'Magic Effect Data\DATA\Actor Value');

  PutEdit(rec, 'Magic Effect Data\DATA\Archtype', 'Value Modifier');

  if av <> '' then begin
    PutEdit(rec, 'Magic Effect Data\DATA\Actor Value', av);
    if not SameText(GetElementEditValues(rec, 'Magic Effect Data\DATA\Actor Value'), av) then
      Problem('ForceValueModifier: ' + EditorID(rec) + ' lost its actor value ('
        + av + ')');
  end else
    Problem('ForceValueModifier: ' + EditorID(rec) + ' had no actor value to keep');

  // The dual pair means nothing to a plain modifier, and a stale second actor
  // value left behind reads like an intent nothing acts on. Only touched if the
  // definition actually names those fields - writing a path that does not exist
  // is how a generator quietly does nothing, which is the fault being fixed
  // here, so it is reported instead.
  if SameText(arch, 'Dual Value Modifier') then begin
    if Assigned(ElementByPath(rec, 'Magic Effect Data\DATA\Second Actor Value')) then
      PutEdit(rec, 'Magic Effect Data\DATA\Second Actor Value', 'None')
    else
      Problem('ForceValueModifier: no Second Actor Value path on ' + EditorID(rec));

    if Assigned(ElementByPath(rec, 'Magic Effect Data\DATA\Second AV Weight')) then
      PutNative(rec, 'Magic Effect Data\DATA\Second AV Weight', 0.0);
  end;

  Say('    ' + EditorID(rec) + ': ' + arch + ' -> Value Modifier');
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

// Carry one enum field across by its EDIT VALUE, source to destination.
//
// For a record we REUSE rather than recopy: everything a fresh copy would have
// supplied has to be written back, or the record keeps whatever the last run
// left in it. Only the enums need this - every other field the builders set by
// hand already.
//
// An empty read is never written. That is not caution for its own sake: an
// Actor Value once became "Aggression" because an empty string went in and the
// enum took index 0.
procedure ForceEnumFrom(dst, src: IwbMainRecord; path: string);
var
  v: string;
begin
  if not Assigned(dst) or not Assigned(src) then Exit;
  v := GetElementEditValues(src, path);
  if v = '' then begin
    Problem('ForceEnumFrom: пусто у ' + EditorID(src) + ' -> ' + path);
    Exit;
  end;
  PutEdit(dst, path, v);
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

// A copy inherits every flag of its source, and one library source is a POISON:
// _RSL_MgefSpeed comes from AlchDamageSpeed, so it arrived Hostile and with
// Resist Value = ResistPoison. It was the only one of the 39 library effects
// carrying either - a hostile, resistable effect sitting on a constant-effect
// self ability, at the mercy of whatever poison resistance the character has.
//
// Same class of mistake as the shieldChargeDamageStamina note above. Clearing
// both on every library effect keeps the library uniform rather than
// special-casing the one record that happened to be wrong.
procedure MakeNonHostile(rec: IwbMainRecord);
var
  el: IInterface;
begin
  if not Assigned(rec) then Exit;

  el := MgefFlags(rec);
  if not Assigned(el) then begin
    Problem('no Flags path on ' + EditorID(rec) + ' - stays hostile');
    Exit;
  end;
  if (GetNativeValue(el) and FLAG_HOSTILE) <> 0 then begin
    SetNativeValue(el, GetNativeValue(el) and not FLAG_HOSTILE);
    Say('    cleared Hostile on ' + EditorID(rec));
  end;

  // Same nil-until-touched problem as Flags, so fall back to the raw DATA
  // subrecord exactly as MgefFlags does.
  el := ElementByPath(rec, 'Magic Effect Data\DATA\Resist Value');
  if not Assigned(el) then begin
    el := ElementBySignature(rec, 'DATA');
    if Assigned(el) then el := ElementByPath(el, 'Resist Value');
  end;
  if not Assigned(el) then begin
    Problem('no Resist Value path on ' + EditorID(rec));
    Exit;
  end;
  if GetNativeValue(el) <> -1 then begin
    Say('    cleared Resist Value (' + GetEditValue(el) + ') on ' + EditorID(rec));
    SetNativeValue(el, -1);
  end;
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
  fresh : Boolean;
begin
  stem := Copy(newEdid, Length(PFX) + 5, Length(newEdid));   // strip "<PFX>Mgef"
  src := RecordByEDID(FileByName(srcFile), 'MGEF', srcEdid);
  if not Assigned(src) then begin
    Problem('CopyVanillaMgef: source not found ' + srcFile + ':' + srcEdid);
    Exit;
  end;
  AddMasterIfMissing(tgt, srcFile);

  // KEEP THE FORMID. This used to drop the record and deep-recopy every run,
  // on the grounds that a fresh copy is the only state worth trusting and that
  // "FormID churn is harmless: the SPELs that reference the library are rebuilt
  // in the same run". Inside one run that is still true. Across releases it is
  // not: a player's save holds these ids, and a rerun that moves them moves
  // the save's references with it.
  //
  // So the record is reused and the shape written back below - see the
  // ForceEnumFrom block after the DATA tree is open. What the old comment
  // warned about (unnavigable DATA, an Actor Value clobbered to "Aggression")
  // is guarded there rather than avoided by starting over.
  old := RecordByEDID(tgt, 'MGEF', newEdid);
  fresh := not Assigned(old);
  if fresh then begin
    Result := wbCopyElementToFile(src, tgt, True, True);
    if not Assigned(Result) then begin
      Problem('CopyVanillaMgef: not copied ' + newEdid);
      Exit;
    end;
  end else
    Result := old;

  ScrubTemplate(Result);           // VMAD/DNAM/KSIZ/KWDA/MDOB
  DropConditions(Result);          // RFAB sources carry Peryite conditions
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

  // Everything the deep copy used to supply, for a record we kept. After the
  // Casting Type / Delivery writes above, because those are what force the
  // DATA tree open.
  if not fresh then begin
    CopyFlagsFrom(Result, src);
    ForceEnumFrom(Result, src, 'Magic Effect Data\DATA\Archtype');
    ForceEnumFrom(Result, src, 'Magic Effect Data\DATA\Actor Value');
  end;

  // Must come after the Casting Type / Delivery writes above: they are what
  // force the DATA tree open on a freshly-copied record.
  MakeNonHostile(Result);
  ForceValueModifier(Result);

  // Library MGEFs are pure mechanics - hidden in the active-effects UI. Each
  // stage SPEL shows a single visible "face" MGEF (BuildFace) cloned off the
  // first of these, carrying the combined description.
  HideInUI(Result);

  if flipDetrimental then
    AddDetrimental(Result);   // proven: ORs the bit, verifies by name via FlagsOf

  Remember(newEdid, Result);
  if fresh then Inc(madeNew) else Inc(reused);
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
  // school cost (cost = 100 - Mod; Detrimental -> Mod negative -> costs more)
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyAlteration',      PFX + 'MgefCostAlt',    True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyConjuration',     PFX + 'MgefCostConj',   True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyDestruction',     PFX + 'MgefCostDest',   True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyIllusion',        PFX + 'MgefCostIllu',   True);
  CopyVanillaMgef('Skyrim.esm', 'MG02FortifyRestoration',     PFX + 'MgefCostRest',   True);
  // Peryite bonuses for wrapper stages 2/3 (stay Fortify)
  CopyVanillaMgef('Skyrim.esm', 'AbResistFrost',              PFX + 'MgefBonusFrost', False);
  // The same vanilla effect flipped Detrimental: a cold you cannot shake
  // leaves you worse at standing the cold, and that is an actor value rather
  // than an invisible coefficient. See AB_AUDIT.md 5.1.
  CopyVanillaMgef('Skyrim.esm', 'AbResistFrost',              PFX + 'MgefFrostWeak',  True);
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

// Edit-value of a vanilla MGEF's Actor Value - copied verbatim onto our record
// so we never have to guess the enum string ("Health Rate Mult" vs ...).
function VanillaAV(srcFile, srcEdid: string): string;
var
  r: IwbMainRecord;
begin
  Result := '';
  r := RecordByEDID(FileByName(srcFile), 'MGEF', srcEdid);
  if Assigned(r) then
    Result := GetElementEditValues(r, 'Magic Effect Data\DATA\Actor Value');
end;

// A beneficial Value-Modifier MGEF: template = BladesAbBlessing (vanilla Fortify
// Health - Recover / No Duration / No Area, NOT Detrimental, shown in UI), only
// the Actor Value is repointed. STABLE FormID (reused by EDID) so a rerun never
// dangles the SPEL EFID that references it - the bug that made the bonuses do
// nothing (ability on the player, effect list empty at runtime).
function AddBuffMgef(tpl: IwbMainRecord; edid, fullName, descr, avSourceEdid: string): IwbMainRecord;
var
  fresh: Boolean;
  av   : string;
begin
  av := VanillaAV('Skyrim.esm', avSourceEdid);
  if av = '' then begin
    Problem('AddBuffMgef: не прочитался Actor Value из ' + avSourceEdid);
    Exit;
  end;

  Result := RecordByEDID(tgt, 'MGEF', edid);
  fresh := not Assigned(Result);
  if fresh then begin
    Result := wbCopyElementToFile(tpl, tgt, True, True);
    if not Assigned(Result) then begin
      Problem('не скопировался MGEF ' + edid);
      Exit;
    end;
  end;

  ScrubTemplate(Result);
  PutEdit(Result, 'EDID', edid);
  PutEdit(Result, 'FULL', fullName);
  PutEdit(Result, 'DNAM', descr);
  // force the shape in case a reused record was an earlier (different) copy
  PutEdit(Result, 'Magic Effect Data\DATA\Archtype',     'Value Modifier');
  PutEdit(Result, 'Magic Effect Data\DATA\Casting Type', 'Constant Effect');
  PutEdit(Result, 'Magic Effect Data\DATA\Delivery',     'Self');
  PutEdit(Result, 'Magic Effect Data\DATA\Actor Value',  av);
  CopyFlagsFrom(Result, tpl);   // Recover + No Duration + No Area, NOT Detrimental
  Say('  buff MGEF ' + edid + ' -> AV "' + av + '"  [' + FlagsOf(Result) + ']');

  Remember(edid, Result);
  if fresh then Inc(madeNew) else Inc(reused);
end;

// Full-bar regen bonuses: THREE separate 1-effect abilities so only the ones
// currently earned show in the active-effects list (a single 3-effect ability
// would render all 3 lines whenever any is active, even at magnitude 0).
// Magnitude (BonusRegenPct) is pushed at runtime by _RSL_Controller.SetBonus.
procedure BuildBonusAbility;
var
  mgefTpl, spelTpl : IwbMainRecord;
  mH, mM, mS       : IwbMainRecord;
begin
  Say('');
  Say('--- bonus abilities (full-bar regen) ---');

  mgefTpl := FindValueModifierTemplate;
  if not Assigned(mgefTpl) then begin
    Say('  no Value-Modifier template - skipping bonus abilities');
    Exit;
  end;

  mH := AddBuffMgef(mgefTpl, PFX + 'MgefBonusHealRegen', L('lib.BonusHealRegen.full'), L('lib.BonusHealRegen.dnam'), 'AbDamageHealRateVisible');
  mM := AddBuffMgef(mgefTpl, PFX + 'MgefBonusMagRegen',  L('lib.BonusMagRegen.full'),  L('lib.BonusMagRegen.dnam'),  'AbDamageMagickaRate');
  mS := AddBuffMgef(mgefTpl, PFX + 'MgefBonusStamRegen', L('lib.BonusStamRegen.full'), L('lib.BonusStamRegen.dnam'), 'AbDamageStaminaRateVisible');
  if not Assigned(mH) or not Assigned(mM) or not Assigned(mS) then Exit;

  spelTpl := FindAbilityTemplate;
  if not Assigned(spelTpl) then begin
    Say('  no ability template - skipping bonus abilities');
    Exit;
  end;
  AddAbility1(spelTpl, PFX + 'AbBonusWarm', L('lib.BonusHealRegen.full'), mH);
  AddAbility1(spelTpl, PFX + 'AbBonusRest', L('lib.BonusMagRegen.full'),  mM);
  AddAbility1(spelTpl, PFX + 'AbBonusFed',  L('lib.BonusStamRegen.full'), mS);
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
// spitType ('Disease' or 'Ability') / Constant Effect / Self, plus an ETYP.
// Templates copied from trap or hostile spells carry the wrong Target Type.
//
// ETYP is SET, not dropped. Dropping it is what broke hypothermia: the three
// _RSL_AbHypo SPELs were added to the player (HasSpell stayed true across a
// reload) and the engine never created a single ActiveEffect for them - not
// even for their non-hostile hidden effects - so no status, no penalty, and
// the native repair loop re-applied them once a second forever without ever
// succeeding. _RSL_AbSleep, built without NormalizeSpit, kept its ETYP and
// worked all along; that was the only structural difference between them.
//
// Vanilla is unanimous: all 250 Ability and all 13 Disease SPELs in Skyrim.esm
// have an ETYP. This mod already learned the same lesson once for powers -
// see _RSL_PowerCampfire, where "an empty ETYP is exactly what makes the RFAB
// menu hand-cast it".
procedure NormalizeSpit(rec: IwbMainRecord; spitType: string);
begin
  if not Assigned(rec) then Exit;
  PutEdit(rec, 'SPIT\Type',        spitType);
  PutEdit(rec, 'SPIT\Cast Type',   'Constant Effect');
  PutEdit(rec, 'SPIT\Target Type', 'Self');
  PutNative(rec, 'ETYP', EQUP_EITHER_HAND);
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

// A vanilla 2-button Message Box MESG to copy (adding the Menu Buttons array
// from scratch is fragile). MessageBox.Show() returns the button index.
function FindMsgBoxTemplate: IwbMainRecord;
var
  grp: IwbGroupRecord;
  r  : IwbMainRecord;
  i  : Integer;
  btns: IInterface;
begin
  Result := nil;
  grp := GroupBySignature(FileByName('Skyrim.esm'), 'MESG');
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if (GetNativeValue(ElementByPath(r, 'DNAM')) and 1) = 1 then begin
      btns := ElementByName(r, 'Menu Buttons');
      if Assigned(btns) and (ElementCount(btns) >= 2) then begin
        Result := r;
        Say('  message-box template: ' + EditorID(r));
        Exit;
      end;
    end;
  end;
  Problem('no 2-button Message Box MESG in Skyrim.esm');
end;

// A yes/no confirm box: copy a message-box template, retarget DESC + button 0/1.
function AddMsgConfirm(tpl: IwbMainRecord; edid, body, btn0, btn1: string): IwbMainRecord;
var
  btns: IInterface;
  i: Integer;
begin
  if not Assigned(tpl) then Exit;
  Result := RecordByEDID(tgt, 'MESG', edid);
  if not Assigned(Result) then begin
    Result := wbCopyElementToFile(tpl, tgt, True, True);
    if not Assigned(Result) then begin Problem('MESG confirm not copied ' + edid); Exit; end;
    PutEdit(Result, 'EDID', edid);
    Inc(madeNew);
  end else
    Inc(reused);
  DropElement(Result, 'ITXT');            // no full-screen title
  DropElement(Result, 'INAM');            // no icon
  PutEdit(Result, 'DESC', body);
  PutNative(Result, 'DNAM', 1);           // Message Box
  btns := ElementByName(Result, 'Menu Buttons');
  if Assigned(btns) then begin
    while ElementCount(btns) > 2 do RemoveByIndex(btns, 2, True);
    for i := 0 to Pred(ElementCount(btns)) do
      DropElement(ElementByIndex(btns, i), 'Conditions');
    if ElementCount(btns) > 0 then PutEdit(ElementByIndex(btns, 0), 'ITXT', btn0);
    if ElementCount(btns) > 1 then PutEdit(ElementByIndex(btns, 1), 'ITXT', btn1);
    Say('  confirm MESG ' + edid + ' buttons=[' + GetElementEditValues(ElementByIndex(btns, 0), 'ITXT')
      + ', ' + GetElementEditValues(ElementByIndex(btns, 1), 'ITXT') + ']');
  end else
    Problem('no Menu Buttons in ' + edid);
  Remember(edid, Result);
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
  fresh   : Boolean;
begin
  Result := nil;
  lib := RecordByEDID(tgt, 'MGEF', PFX + libStem);
  if not Assigned(lib) then begin
    Problem('BuildFace: no library MGEF ' + PFX + libStem + ' for ' + faceEdid);
    Exit;
  end;
  // KEEP THE FORMID: a save holds the id of the face effect it is showing, so
  // a rerun that recreates the record moves the save's reference with it. The
  // record is reused and the clone's own contribution - archetype, actor value
  // and flags - written back from the library below.
  old := RecordByEDID(tgt, 'MGEF', faceEdid);
  fresh := not Assigned(old);
  if fresh then begin
    Result := wbCopyElementToFile(lib, tgt, True, True);
    if not Assigned(Result) then begin
      Problem('BuildFace: not copied ' + faceEdid);
      Exit;
    end;
  end else
    Result := old;

  PutEdit(Result, 'EDID', faceEdid);
  PutEdit(Result, 'FULL', fullName);
  PutEdit(Result, 'DNAM', dnam);
  PutEdit(Result, 'Magic Effect Data\DATA\Casting Type', 'Constant Effect');
  PutEdit(Result, 'Magic Effect Data\DATA\Delivery',     'Self');

  // What the clone used to supply. libStem depends on the stage's first spec
  // token, so a reused face can be a copy of a DIFFERENT library effect than
  // the one it should carry now - this is what puts it right.
  if not fresh then begin
    CopyFlagsFrom(Result, lib);
    ForceEnumFrom(Result, lib, 'Magic Effect Data\DATA\Archtype');
    ForceEnumFrom(Result, lib, 'Magic Effect Data\DATA\Actor Value');
  end;

  // The face is a copy of a library entry, so it inherits whatever that one
  // carried - including the archetype and, until DropConditions existed, the
  // condition. Both are settled here as well: a reused face is not re-copied,
  // so CopyFlagsFrom above can only put back what the source has now.
  DropConditions(Result);
  ForceValueModifier(Result);

  ShowInUI(Result);   // the library MGEF is hidden; the face must show
  Remember(faceEdid, Result);
  if fresh then Inc(madeNew) else Inc(reused);
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

// The hidden marker that keeps an advanced illness legible to the world.
//
// Stages 2 and 3 are abilities, so the engine stops seeing an advanced illness
// as a disease at all - and the vanilla WICommentDiseased quest, which is what
// makes strangers remark on a sick traveller, tests exactly that (condition 39,
// GetDisease). This spell is Type=Disease and carries nothing else, so it keeps
// that answer true while showing the player nothing.
//
// IT CARRIES ONE EFFECT, NOT NONE, and that is the whole point of writing this
// by hand instead of calling AddStageSpell with an empty spec. An empty spec
// generates cleanly, but it produces a SPEL the engine instantiates nothing
// for - and this mod has already paid for that once: e275a60 found hypothermia
// "never instantiated", where AddSpell was accepted and HasSpell stayed true
// while no ActiveEffect was ever created. Whether GetDisease reads the spell
// list or the active effects is the engine's business and cannot be read out of
// the data, so the marker satisfies both readings.
//
// The effect is a library MGEF at magnitude ZERO: library effects are already
// Hide-in-UI (see the builder), and a zero magnitude changes nothing while
// still instantiating - RFAB's own diseases use exactly that trick for their
// description carriers. HealRateMult is chosen because nothing in this mod or
// in RFAB gates on it; SpeedMult would have been a poor choice, since RFAB
// locks the player in place at SpeedMult <= 0.
procedure BuildDiseaseMarker(disTpl: IwbMainRecord);
var
  rec     : IwbMainRecord;
  effects : IInterface;
  fresh   : Boolean;
begin
  rec := RecordByEDID(tgt, 'SPEL', PFX + 'DiseaseMarker');
  fresh := not Assigned(rec);
  if fresh then begin
    rec := wbCopyElementToFile(disTpl, tgt, True, True);
    if not Assigned(rec) then begin
      Problem('disease marker SPEL not copied');
      Exit;
    end;
  end;
  ScrubTemplate(rec);
  PutEdit(rec, 'EDID', PFX + 'DiseaseMarker');
  PutEdit(rec, 'FULL', L('dz.Marker.name'));
  PutEdit(rec, 'DESC', '');
  NormalizeSpit(rec, 'Disease');

  effects := ElementByName(rec, 'Effects');
  while Assigned(effects) and (ElementCount(effects) > 0) do
    RemoveByIndex(effects, 0, True);
  AppendLibEffectsFrom(effects, 'MgefHealRegen=0', PFX + 'DiseaseMarker', 0);

  Remember(PFX + 'DiseaseMarker', rec);
  if fresh then Inc(madeNew) else Inc(reused);
end;

// Three disease-type stage SPELs + 4 MESG. All text from strings.txt
// (dz.<key>.name.N / .flavour.N / .msg.*); the numeric penalties come from the
// per-stage spec args and are auto-appended by AddStageSpell.
procedure BuildDiseaseTriad(disTpl: IwbMainRecord; key, spec1, spec2, spec3: string);
begin
  // STAGE 1 IS A DISEASE. STAGES 2 AND 3 ARE ABILITIES, DELIBERATELY.
  //
  // The engine's Cure Disease strips every Type=Disease spell on the actor and
  // asks nobody, so the type IS the cure switch. Stage 1 keeps it and stays
  // curable by potion, spell or altar - that is the reward for noticing early.
  // Past that a cure must not undo the illness, so the stages are abilities and
  // the engine cannot see them: no strip, no P reset, no spurious notification.
  //
  // What that costs is the townsfolk remarking on a sick traveller: the vanilla
  // WICommentDiseased quest tests condition 39, GetDisease, which reads the
  // spell type. _RSL_DiseaseMarker below buys it back.
  AddStageSpell(disTpl, PFX + 'Disease' + key + '1', L('dz.' + key + '.name.1'), 'Disease', L('dz.' + key + '.flavour.1'), spec1);
  AddStageSpell(disTpl, PFX + 'Disease' + key + '2', L('dz.' + key + '.name.2'), 'Ability', L('dz.' + key + '.flavour.2'), spec2);
  AddStageSpell(disTpl, PFX + 'Disease' + key + '3', L('dz.' + key + '.name.3'), 'Ability', L('dz.' + key + '.flavour.3'), spec3);
  AddMsg(PFX + 'Msg' + key + '1',     L('dz.' + key + '.msg.contract'));
  AddMsg(PFX + 'Msg' + key + '2',     L('dz.' + key + '.msg.2'));
  AddMsg(PFX + 'Msg' + key + '3',     L('dz.' + key + '.msg.3'));
  AddMsg(PFX + 'Msg' + key + 'Cured', L('dz.' + key + '.msg.cured'));
  // Stepping DOWN a stage said nothing at all before - v0.4.0 announces a
  // worsening and a cure and stays silent when an illness eases, so the player
  // watched a disease improve with no feedback. One message per downward step.
  AddMsg(PFX + 'Msg' + key + 'Ease2', L('dz.' + key + '.msg.ease.2'));
  AddMsg(PFX + 'Msg' + key + 'Ease1', L('dz.' + key + '.msg.ease.1'));
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
  // Same gap as the diseases: dropping from severe to moderate was silent.
  AddMsg(PFX + 'MsgHypoEase2',  L('hy.msg.ease.2'));
  AddMsg(PFX + 'MsgHypoEase1',  L('hy.msg.ease.1'));
end;

// Our 5 own diseases, 3 stages each, effects from the penalty library
// (BuildPenaltyLib). Controller-side multipliers (cold-tolerance, sleep
// efficiency, hunger accrual, food restore) are NOT effects - see the balance
// spec in magical-seeking-garden.md.
// --- COBJ: boil water ------------------------------------------------------
//
// A recipe with no components at all. That shape is real and vanilla uses it -
// HearthFires' drafting-table layouts have no CNTO, and neither do two of the
// Dark Brotherhood tempering recipes - and here it says the right thing: some
// vessel is always to hand, and what you actually need is a fire, a pot and a
// kettle.
//
// The gate is the two conditions instead. The kettle is never consumed:
// carrying it IS the cost, and the same kettle is what Campfire::Light asks for
// before it hangs a pot over the fire. One item, one rule, two places.
//
// The water itself is RFAB's own record, so the bandage recipe RFAB already
// ships keeps working unchanged. What stops a pot over a fire from becoming an
// income is RfabPatch::MakeWaterUnsellable, which puts VendorNoSale on it at
// runtime.
procedure BuildWaterRecipe;
var
  co, tpl, water, kwd, kettle, perk: IwbMainRecord;
  items, cond: IInterface;
begin
  Say('');
  Say('--- COBJ: вскипятить воду ---');

  water  := RecordByEDID(FileByName('RFAB.esp'),   'ALCH', 'RFAB_Drink_Other_Water');
  perk   := RecordByEDID(FileByName('RFAB.esp'),   'PERK', 'RFAB_Perk_Survival_BaseSurvival');
  kettle := RecordByEDID(FileByName('Skyrim.esm'), 'MISC', 'Kettle01');
  kwd    := RecordByEDID(FileByName('Skyrim.esm'), 'KYWD', 'CraftingCookpot');

  if not Assigned(water)  then begin Problem('RFAB_Drink_Other_Water не найден'); Exit; end;
  if not Assigned(perk)   then begin Problem('RFAB_Perk_Survival_BaseSurvival не найден'); Exit; end;
  if not Assigned(kettle) then begin Problem('Kettle01 не найден');          Exit; end;
  if not Assigned(kwd)    then begin Problem('CraftingCookpot не найден');   Exit; end;

  AddMasterIfMissing(tgt, 'RFAB.esp');

  co := RecordByEDID(tgt, 'COBJ', PFX + 'RecipeWater');
  if Assigned(co) then begin
    Inc(reused);
  end else begin
    // A vanilla cookpot recipe as the shell: BNAM is already CraftingCookpot
    // and the record is the right shape.
    tpl := RecordByEDID(FileByName('Skyrim.esm'), 'COBJ', 'RecipeFoodChickenCooked');
    if not Assigned(tpl) then begin Problem('шаблон RecipeFoodChickenCooked не найден'); Exit; end;
    co := wbCopyElementToFile(tpl, tgt, True, True);
    if not Assigned(co) then begin Problem('RecipeWater COBJ не скопирован'); Exit; end;
    PutEdit(co, 'EDID', PFX + 'RecipeWater');
    Inc(madeNew);
  end;

  // No components: drop the container outright rather than leaving it empty,
  // which is what the vanilla recipes without components look like.
  items := ElementByName(co, 'Items');
  if Assigned(items) then Remove(items);
  DropElement(co, 'COCT');

  cond := ElementByName(co, 'Conditions');
  if Assigned(cond) then
    while ElementCount(cond) > 0 do RemoveByIndex(cond, 0, True);

  // PutFormID, not PutNative: the water lives in RFAB.esp, and that is the
  // one case a raw formID gets wrong. See the comment on PutFormID.
  PutFormID(co, 'CNAM', water);
  PutFormID(co, 'BNAM', kwd);
  PutNative(co, 'NAM1', 1);

  AddCond(co, CTDA_FUNC_HASPERK,      CTDA_OP_EQ, 1.0, perk);
  AddCond(co, CTDA_FUNC_GETITEMCOUNT, CTDA_OP_GE, 1.0, kettle);

  Remember(PFX + 'RecipeWater', co);

  Say('  water COBJ ' + LocalIDHex(co) + ': BNAM="' + GetElementEditValues(co, 'BNAM')
    + '" CNAM="' + GetElementEditValues(co, 'CNAM')
    + '" NAM1=' + GetElementEditValues(co, 'NAM1')
    + ' условий=' + IntToStr(ElementCount(ElementByName(co, 'Conditions'))));
end;

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

  // Common cold -> pneumonia.
  //
  // Stages 2 and 3 carry an explicit FrostResist penalty rather than the old
  // invisible cold-tolerance multiplier (x0.85/0.7/0.5, controller-side). The
  // climate model was rewritten, so those numbers meant nothing any more, and
  // an actor value is both visible to the player and tunable against the same
  // formula as everything else that resists cold. AB_AUDIT.md 5.1.
  BuildDiseaseTriad(disTpl, 'ColdCommon',
    'MgefMagRegen=10',
    'MgefFrostWeak=25,MgefMagRegen=40,MgefStamRegen=25',
    'MgefFrostWeak=50,MgefMaxStamina=15,MgefMagRegen=70,MgefStamRegen=50');

  // The hidden marker - see BuildDiseaseMarker for what it is and why.
  BuildDiseaseMarker(disTpl);

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
// SPIT forced to Type=Ability, only FULL renamed. The controller swaps
// base <-> our stage spell and drives P. A cure no longer walks these back at
// all: stage 1 is RFAB's disease and answers to medicine, stages 2 and 3 are
// abilities and only sleep, food and warmth reach them.

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

// Stage 2/3 wrapper SPEL = RFAB's stage-1 debuff effects (copied 1:1, still
// visible under RFAB's own names) + our extras. RFAB's Peryite-conditional
// bonus effects are DROPPED - our aggravated stages are pure debuff. A Peryite
// follower keeps the boon only at stage 1 (RFAB's own record); the controller
// freezes the disease there. The extras collapse to one visible "face" line
// (the renamed stage + auto-built text); the remaining extras stay hidden.
// "a. " + "b." -> "a. b." ; either side may be empty.
function JoinSentences(a, b: string): string;
begin
  a := Trim(a);
  b := Trim(b);
  if a = '' then Result := b
  else if b = '' then Result := a
  else Result := a + ' ' + b;
end;

// Strip RFAB's visible description-carrier effect (zero magnitude, Actor Value
// "None" - it applies nothing, it only shows a line) and hand back its text.
// Real mechanical effects are left alone even when visible.
function TakeDescriptionCarrier(effs: IInterface; ctx: string): string;
var
  i  : Integer;
  eff, fl: IInterface;
  mg : IwbMainRecord;
  hidden: Boolean;
begin
  Result := '';
  if not Assigned(effs) then Exit;
  for i := Pred(ElementCount(effs)) downto 0 do begin
    eff := ElementByIndex(effs, i);
    mg  := LinksTo(ElementByPath(eff, 'EFID'));
    if not Assigned(mg) then Continue;
    fl := MgefFlags(mg);
    hidden := Assigned(fl) and ((GetNativeValue(fl) and $00008000) <> 0);
    if (not hidden)
       and (GetNativeValue(ElementByPath(eff, 'EFIT\Magnitude')) < 0.0001)
       and SameText(GetElementEditValues(mg, 'Magic Effect Data\DATA\Actor Value'), 'None') then begin
      if Result = '' then Result := Trim(GetElementEditValues(mg, 'DNAM'));
      Say('    ' + ctx + ': dropped description carrier ' + EditorID(mg));
      RemoveByIndex(effs, i, True);
    end;
  end;
end;

// "Aggravated stages carry debuffs only." RFAB pairs every disease penalty with
// a boon - fortify armour, restore magicka, resist stagger - and the boons are
// Peryite-gated. The Detrimental flag separates the two exactly: in RFAB's
// records every penalty carries it and no boon does (verified against
// DumpRfabDiseaseEffects for all 7), so it is the discriminator we filter on.
//
// It is NOT the gate itself. The dump shows RFAB uses no vanilla gating here:
// no CTDA on the MGEF, no Perk to Apply. The only structural tell is that every
// boon carries KWDA keywords and no penalty does, i.e. RFAB resolves it in its
// own framework by keyword. Filtering on Detrimental keeps us independent of
// that.
procedure DropNonDetrimental(effs: IInterface; ctx: string);
var
  i : Integer;
  mg: IwbMainRecord;
begin
  if not Assigned(effs) then Exit;
  for i := Pred(ElementCount(effs)) downto 0 do begin
    mg := LinksTo(ElementByPath(ElementByIndex(effs, i), 'EFID'));
    if not Assigned(mg) then Continue;
    if Pos('Detrimental', FlagsOf(mg)) = 0 then begin
      Say('    ' + ctx + ': dropped boon ' + EditorID(mg));
      RemoveByIndex(effs, i, True);
    end;
  end;
end;

function CloneRfabDisease(disTpl, src: IwbMainRecord;
  newEdid, newFull, flavour, inheritText, extraSpec: string): IwbMainRecord;
var
  dst, face : IwbMainRecord;
  effs, e: IInterface;
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
  // Ability, not Disease - see the note in BuildDiseaseTriad. RFAB's own stage
  // 1 stays a disease and stays curable; only the two stages this layer adds
  // are out of the engine's reach.
  NormalizeSpit(dst, 'Ability');   // trap template is Constant Effect / Touch

  CopyEffectsFrom(dst, src);
  effs := ElementByName(dst, 'Effects');

  // NOTE: there is no condition to filter on - neither the effect entry nor the
  // MGEF carries a CTDA, and Perk to Apply is empty. RFAB gates its boons by
  // keyword inside its own framework. We filter on the Detrimental flag, which
  // states the intent directly: an aggravated stage carries penalties only.

  // RFAB's disease records have a fixed shape: several HIDDEN mechanical
  // effects plus ONE visible zero-magnitude carrier (RFAB_Description_<X>)
  // that exists only to put a name and a description in the active-effects
  // list. Our clone brings its own "face" effect for that job, so the carrier
  // has to go - otherwise one disease shows as two lines.
  TakeDescriptionCarrier(effs, newEdid);
  DropNonDetrimental(effs, newEdid);

  // The surviving inherited penalties are hidden and wordless, so they need
  // describing by hand (wrap.<key>.inherit) - that is the text the player was
  // missing. RFAB's own carrier text is NOT reused: it also advertises the
  // boons we just dropped.
  PutEdit(dst, 'DESC', JoinSentences(inheritText,
    Trim(flavour + ' ' + L('wrap.aggravated') + ' ' + SpecToText(extraSpec, 0, False))));
  descr := JoinSentences(inheritText,
    Trim(flavour + ' ' + L('wrap.aggravated') + ' ' + SpecToText(extraSpec, 0, True)));

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

// Diagnostic: dump what RFAB's disease record actually carries. Our stage 2/3
// clones keep every non-CTDA effect, so this is exactly the set that applies on
// top of our own extras - and, for anything not Hide-in-UI, renders as its own
// line in the active-effects list next to our "face" line. The wrapper's DESC
// currently describes ONLY the extras, so this is the list it fails to mention.
// Everything on an MGEF that could gate it. Conditions sit under the CTDA
// signature, NOT in a container called "Conditions" - looking only for the
// latter silently reports "no conditions" on a gated effect.
//
// For RFAB's disease effects the answer turned out to be none of the usual
// suspects: no CTDA, no Perk to Apply. What separates a boon from a penalty in
// its records is that boons carry KEYWORDS and penalties do not, so those get
// printed too, along with any attached script.
procedure DumpMgefGate(mg: IwbMainRecord);
var
  c, one, kw: IInterface;
  j    : Integer;
  names: string;
  k    : IwbMainRecord;
begin
  c := ElementBySignature(mg, 'CTDA');
  if not Assigned(c) then c := ElementByName(mg, 'Conditions');
  if Assigned(c) then begin
    if ElementCount(c) > 0 then
      for j := 0 to Pred(ElementCount(c)) do
        Say('          cond[' + IntToStr(j) + '] ' + Trim(GetEditValue(ElementByIndex(c, j))))
    else
      Say('          cond: ' + Trim(GetEditValue(c)));
  end;

  kw := ElementByName(mg, 'KWDA');
  if Assigned(kw) then begin
    names := '';
    for j := 0 to Pred(ElementCount(kw)) do begin
      k := LinksTo(ElementByIndex(kw, j));
      if Assigned(k) then names := names + ' ' + EditorID(k)
      else names := names + ' <?>';
    end;
    Say('          KWDA:' + names);
  end;

  one := ElementByPath(mg, 'VMAD\Scripts');
  if Assigned(one) then begin
    names := '';
    for j := 0 to Pred(ElementCount(one)) do
      names := names + ' ' + GetElementEditValues(ElementByIndex(one, j), 'scriptName');
    Say('          VMAD:' + names);
  end;

  if not Assigned(c) and not Assigned(kw) then begin
    names := '';
    for j := 0 to Pred(ElementCount(mg)) do
      names := names + ' ' + Name(ElementByIndex(mg, j));
    Say('          no gate found; MGEF elements:' + names);
  end;
end;

procedure DumpRfabDiseaseEffects(src: IwbMainRecord; key: string);
var
  effs, eff, fl: IInterface;
  mg  : IwbMainRecord;
  i   : Integer;
  line, perk: string;
begin
  effs := ElementByName(src, 'Effects');
  if not Assigned(effs) then begin
    Say('  [' + key + '] no Effects on ' + EditorID(src));
    Exit;
  end;
  Say('  [' + key + '] inherited from ' + EditorID(src) + ':');
  for i := 0 to Pred(ElementCount(effs)) do begin
    eff := ElementByIndex(effs, i);
    mg  := LinksTo(ElementByPath(eff, 'EFID'));
    if not Assigned(mg) then begin
      Say('    [' + IntToStr(i) + '] <EFID не разрешился>');
      Continue;
    end;
    line := '    [' + IntToStr(i) + '] ' + EditorID(mg)
          + '  mag=' + GetElementEditValues(eff, 'EFIT\Magnitude')
          + '  AV="' + GetElementEditValues(mg, 'Magic Effect Data\DATA\Actor Value') + '"'
          + '  arch=' + GetElementEditValues(mg, 'Magic Effect Data\DATA\Archtype');
    fl := MgefFlags(mg);
    if Assigned(fl) and ((GetNativeValue(fl) and $00008000) <> 0) then
      line := line + '  HIDDEN'
    else
      line := line + '  VISIBLE name="' + GetElementEditValues(mg, 'FULL') + '"';
    if Pos('Detrimental', FlagsOf(mg)) > 0 then
      line := line + '  DETRIMENTAL';
    perk := GetElementEditValues(mg, 'Magic Effect Data\DATA\Perk to Apply');
    if (perk <> '') and (Pos('NULL', perk) = 0) then
      line := line + '  perk=' + perk;
    Say(line);
    DumpMgefGate(mg);
  end;
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
  DumpRfabDiseaseEffects(src, key);
  CloneRfabDisease(disTpl, src, PFX + 'Dz' + key + '2',
    L('wrap.progressive') + ' ' + baseName, L('wrap.' + key + '.flavour.2'),
    L('wrap.' + key + '.inherit'), spec2);
  CloneRfabDisease(disTpl, src, PFX + 'Dz' + key + '3',
    L('wrap.severe') + ' ' + baseName, L('wrap.' + key + '.flavour.3'),
    L('wrap.' + key + '.inherit'), spec3);
  AddMsg(PFX + 'MsgDz' + key + '2',     baseName + L('wrap.msg.2'));
  AddMsg(PFX + 'MsgDz' + key + '3',     baseName + L('wrap.msg.3'));
  AddMsg(PFX + 'MsgDz' + key + 'Ease2', baseName + L('wrap.msg.ease.2'));
  AddMsg(PFX + 'MsgDz' + key + 'Ease1', baseName + L('wrap.msg.ease.1'));
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
  // extras = library effects appended on top of RFAB's stage-1 effects, which
  // are copied 1:1 minus the Peryite-gated ones. NOTE: those inherited effects
  // apply but are NOT mentioned in the stage description - see
  // DumpRfabDiseaseEffects. Magnitudes are first-pass - tune in playtest.
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

  // Nothing of ours belongs in EFSH. One lived here briefly and does not any
  // more; anything found under our prefix is left over from a run that built it.
  grp := GroupBySignature(tgt, 'EFSH');
  if Assigned(grp) then
    for i := Pred(ElementCount(grp)) downto 0 do begin
      r := ElementByIndex(grp, i);
      ed := EditorID(r);
      if Pos(PFX, ed) = 1 then begin
        Say('    removed EFSH ' + ed);
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

  // Sound descriptors are one per FILE now; the shape before it was one per
  // voice, and those four names are no longer built. Rather than list them,
  // the rule is the general one: every SNDR of ours is rebuilt and remembered
  // on every run, so one under our prefix that nothing remembered is either
  // left over from an older shape or half-built by a run that gave up.
  grp := GroupBySignature(tgt, 'SNDR');
  if Assigned(grp) then
    for i := Pred(ElementCount(grp)) downto 0 do begin
      r := ElementByIndex(grp, i);
      ed := EditorID(r);
      if (Pos(PFX, ed) = 1) and (ids.Values[ed] = '') then begin
        Say('    removed SNDR ' + ed);
        Remove(r);
      end;
    end;

  // Every one of ours, unconditionally: settings moved to an ini the native
  // plugin reads, so a GLOB left behind is a value nothing reads and nothing
  // writes - and the MCM would still be able to find it.
  grp := GroupBySignature(tgt, 'GLOB');
  if Assigned(grp) then begin
    for i := Pred(ElementCount(grp)) downto 0 do begin
      r := ElementByIndex(grp, i);
      ed := EditorID(r);
      if Pos(PFX, ed) = 1 then begin
        Say('    removed GLOB ' + ed);
        Remove(r);
      end;
    end;
    if ElementCount(grp) = 0 then
      Remove(grp);
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
  if not Assigned(vmad) then
    vmad := Add(rec, 'VMAD - Virtual Machine Adapter', True);   // MSTT needs the full name
  if not Assigned(vmad) then begin
    Problem('VMAD not created in ' + EditorID(rec)
      + ' - add it by hand (right-click -> Add -> VMAD) and rerun');
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

// Single-effect ability (AddAbility builds three). The effect is REBUILT on
// every run, new or reused, and stays that way now that CopyVanillaMgef keeps
// its FormIDs: the rebuild is what carries a changed library effect or a
// changed magnitude onto an ability that already exists, and it costs one
// element assignment. It also used to be load-bearing for a worse reason - a
// churned library id left the SPEL's EFID dangling and the ability did
// nothing at runtime. That hazard is gone; the rebuild is kept on its own
// merits. Same shape as AddAbility.
function AddAbility1(tpl: IwbMainRecord; edid: string; fullName: string;
                     mgef: IwbMainRecord): IwbMainRecord;
var
  effects, e: IInterface;
  fresh     : Boolean;
begin
  if not Assigned(mgef) then begin
    Problem('нет эффекта для ' + edid);
    Exit;
  end;

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
  if fresh then Inc(madeNew) else Inc(reused);
end;

// Template for the campfire power. Prefer RFAB_Spell_BecomeTank (RFAB.esp): it
// is a known-working Lesser Power in this pack and carries ETYP = Voice, which
// the RFAB talents menu needs to slot a power (without it the spell hand-casts).
// Falls back to the first vanilla Lesser Power, then the ability template.
function FindLesserPowerTemplate: IwbMainRecord;
var
  src : IwbFile;
  grp : IwbGroupRecord;
  r   : IwbMainRecord;
  i   : Integer;
  styp: string;
begin
  Result := RecordByEDID(FileByName('RFAB.esp'), 'SPEL', 'RFAB_Spell_BecomeTank');
  if Assigned(Result) then begin
    AddMasterIfMissing(tgt, 'RFAB.esp');
    Say('  lesser-power template: RFAB_Spell_BecomeTank (RFAB.esp) - has Voice ETYP');
    Exit;
  end;
  src := FileByName('Skyrim.esm');
  if not Assigned(src) then Exit;
  grp := GroupBySignature(src, 'SPEL');
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    styp := GetElementEditValues(r, 'SPIT\Type');
    if (Pos('lesser', LowerCase(styp)) > 0) and (Pos('power', LowerCase(styp)) > 0) then begin
      Result := r;
      Say('  lesser-power SPEL template: ' + EditorID(r) + '  SPIT Type="' + styp + '"');
      Exit;
    end;
  end;
  Say('  no Lesser Power SPEL found - using ability template + PutEdit');
end;

// First Skyrim.esm ACTI that has a Model and no script / destruction data - a
// clean structural template to copy (a from-scratch ACTI has no Model struct
// and PutEdit cannot create the nested Model\MODL).
function FindActiTemplate: IwbMainRecord;
var
  grp: IwbGroupRecord;
  r  : IwbMainRecord;
  i  : Integer;
begin
  Result := nil;
  grp := GroupBySignature(FileByName('Skyrim.esm'), 'ACTI');
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if Assigned(ElementByName(r, 'Model'))
       and not Assigned(ElementByPath(r, 'VMAD'))
       and not Assigned(ElementByName(r, 'Destructible')) then begin
      Result := r;
      Say('  ACTI template: ' + EditorID(r));
      Exit;
    end;
  end;
end;

// Set an enum sub-field by label without logging a PROBLEM on a bad label -
// returns whether the value now reads back as `v`. Lets BuildCampfire probe
// which label string this xEdit build accepts.
function SetSpitField(rec: IwbMainRecord; path, v: string): Boolean;
var el: IInterface;
begin
  Result := False;
  el := ElementByPath(rec, path);
  if not Assigned(el) then Exit;
  try
    SetEditValue(el, v);
  except
    Exit;
  end;
  Result := SameText(GetElementEditValues(rec, path), v);
end;

// Campfire lesser power (feature 5/6). A script-archetype MGEF (Fire and Forget
// / Self, hidden) carrying _RSL_CampfireEffect, on a Lesser Power SPEL. Runtime
// (the script) does the perk / fuel / cooldown checks, PlaceAtMe, navmesh snap
// and lifetime tracking; the generator just builds the forms. Also emits the
// v0.3.0 notifications.
procedure BuildCampfire;
var
  mgefTpl, spelTpl, mgef, spel, cfLit, cfSrc, flst: IwbMainRecord;
  effects, e, mfl, flstItems, srcEl: IInterface;
  poweredTpl, ffTpl: Boolean;
begin
  Say('');
  Say('--- campfire lesser power ---');

  // _RSL_CampfireLit: an ACTIVATOR wearing the Campfire01Burning model, so it
  // renders fire + light AND carries _RSL_CampfirePlaced (activate -> put it
  // out). ACTI takes a VMAD from the generator (MSTT would not) and, unlike the
  // vanilla MSTT campfire, an activator responds to the activate key.
  cfLit := RecordByEDID(tgt, 'ACTI', PFX + 'CampfireLit');
  // a from-scratch ACTI (or a prior broken one) has no Model - drop it and copy
  // a template that does
  if Assigned(cfLit) and (GetElementEditValues(cfLit, 'Model\MODL') = '') then begin
    // The one place a record's FormID still moves, and only as a repair:
    // a modelless ACTI is unusable, so a new id is the lesser harm. A save
    // that had one placed loses it. Never make this the normal path.
    Say('  _RSL_CampfireLit has no model - recreating from a template');
    Remove(cfLit);
    cfLit := nil;
  end;
  if not Assigned(cfLit) then begin
    cfSrc := FindActiTemplate;
    if not Assigned(cfSrc) then Problem('no ACTI template with a Model in Skyrim.esm')
    else begin
      cfLit := wbCopyElementToFile(cfSrc, tgt, True, True);
      if not Assigned(cfLit) then Problem('_RSL_CampfireLit ACTI not copied')
      else begin
        ScrubTemplate(cfLit);
        PutEdit(cfLit, 'EDID', PFX + 'CampfireLit');
        Inc(madeNew);
      end;
    end;
  end else
    Inc(reused);
  if Assigned(cfLit) then begin
    PutEdit(cfLit, 'FULL', L('camp.name'));
    PutEdit(cfLit, 'Model\MODL', 'Clutter\WoodFires\Campfire01Burning.nif');
    PutNative(cfLit, 'SNAM', $0003F204);   // FXFireCampfireLPSD looping sound
    if GetElementEditValues(cfLit, 'Model\MODL') <> 'Clutter\WoodFires\Campfire01Burning.nif' then
      Problem('_RSL_CampfireLit ACTI: MODL still wrong ("'
        + GetElementEditValues(cfLit, 'Model\MODL') + '")');
    // No script. Putting the fire out on activation is a native
    // TESActivateEvent now (Core/Events.cpp -> Campfire::OnActivated), which
    // asks the same MESG the Papyrus version asked. Dropping VMAD is what
    // unbinds it on a plugin that was generated before this.
    DropElement(cfLit, 'VMAD');
    Remember(PFX + 'CampfireLit', cfLit);
    flst := RecordByEDID(tgt, 'FLST', PFX + 'FireSources');
    if Assigned(flst) then begin
      flstItems := ElementByName(flst, 'FormIDs');
      if Assigned(flstItems) then begin
        FlstAddRecord(flstItems, cfLit);
        FlstRequireResolved(flstItems, PFX + 'FireSources');
      end;
    end;
    Say('  _RSL_CampfireLit ACTI ready ["' + GetElementEditValues(cfLit, 'Model\MODL')
      + '"], added to fire list');
  end;

  // A Constant-Effect script MGEF, kept as the template even though no script
  // is attached any more: the native core recognises the fire by watching for
  // this effect being applied, and a "Fire and Forget" template does not
  // deliver reliably enough to be worth the change.
  mgefTpl := FindScriptArchetypeTemplate;
  ffTpl := False;
  spelTpl := FindLesserPowerTemplate;
  poweredTpl := Assigned(spelTpl);
  if not poweredTpl then spelTpl := FindAbilityTemplate;
  if not Assigned(mgefTpl) or not Assigned(spelTpl) then begin
    Say('  no templates - skipping campfire');
    Exit;
  end;

  // script effect
  mgef := RecordByEDID(tgt, 'MGEF', PFX + 'MgefLightCampfire');
  if Assigned(mgef) then begin
    ScrubTemplate(mgef);
    // The template it was copied from brought a condition with it, like every
    // other record copied in this file.
    DropConditions(mgef);
    Inc(reused);
  end else begin
    mgef := wbCopyElementToFile(mgefTpl, tgt, True, True);
    if not Assigned(mgef) then begin Problem('campfire MGEF not copied'); Exit; end;
    ScrubTemplate(mgef);
    PutEdit(mgef, 'EDID', PFX + 'MgefLightCampfire');
    PutEdit(mgef, 'FULL', L('power.campfire.full'));
    Inc(madeNew);
  end;
  // keep the template's Casting Type = Constant Effect (script self-dispels)
  PutEdit(mgef, 'Magic Effect Data\DATA\Delivery', 'Self');   // proven label (AddBuffMgef)
  // known-good flags: Hide in UI only - a clean self effect, not hostile /
  // detrimental (the template may carry other bits).
  mfl := MgefFlags(mgef);
  if Assigned(mfl) then
    SetNativeValue(mfl, $00008000);
  Say('  campfire MGEF: Arch="' + GetElementEditValues(mgef, 'Magic Effect Data\DATA\Archtype')
    + '" Cast="' + GetElementEditValues(mgef, 'Magic Effect Data\DATA\Casting Type')
    + '" Deliv="' + GetElementEditValues(mgef, 'Magic Effect Data\DATA\Delivery')
    + '" flags=[' + FlagsOf(mgef) + ']');
  // No script. The native core watches for this effect being applied and
  // lights the fire itself (Core/Elemental.cpp -> Campfire::Light).
  DropElement(mgef, 'VMAD');
  Remember(PFX + 'MgefLightCampfire', mgef);

  // lesser-power spell
  spel := RecordByEDID(tgt, 'SPEL', PFX + 'PowerCampfire');
  if Assigned(spel) then begin
    ScrubTemplate(spel);
    Inc(reused);
  end else begin
    spel := wbCopyElementToFile(spelTpl, tgt, True, True);
    if not Assigned(spel) then begin Problem('campfire SPEL not copied'); Exit; end;
    ScrubTemplate(spel);
    Inc(madeNew);
  end;
  PutEdit(spel, 'EDID', PFX + 'PowerCampfire');
  PutEdit(spel, 'FULL', L('power.campfire.full'));
  PutEdit(spel, 'DESC', L('power.campfire.desc'));
  Say('  PowerCampfire SPIT before: Type="' + GetElementEditValues(spel, 'SPIT\Type')
    + '" Cast="' + GetElementEditValues(spel, 'SPIT\Cast Type')
    + '" ETYP="' + GetElementEditValues(spel, 'ETYP') + '"');
  // Keep the template's ETYP (Voice) - that is what slots it as a power.
  // Only coerce SPIT if we fell back to a non-power template.
  if not poweredTpl then
    if not SetSpitField(spel, 'SPIT\Type', 'Lesser Power') then
      SetSpitField(spel, 'SPIT\Type', 'Power');
  // Constant Effect / Self, to match the MGEF (script self-dispels). PutEdit
  // with 'Constant Effect' is the proven path (NormalizeSpit uses it).
  PutEdit(spel, 'SPIT\Cast Type',   'Constant Effect');
  PutEdit(spel, 'SPIT\Target Type', 'Self');
  // ETYP = Voice (Skyrim.esm 00025BEE) - set unconditionally: a reused
  // _RSL_PowerCampfire from an older run never gets the new template's ETYP,
  // and an empty ETYP is exactly what makes the RFAB menu hand-cast it.
  PutNative(spel, 'ETYP', $00025BEE);
  Say('  PowerCampfire SPIT after:  Type="' + GetElementEditValues(spel, 'SPIT\Type')
    + '" Cast="' + GetElementEditValues(spel, 'SPIT\Cast Type')
    + '" Target="' + GetElementEditValues(spel, 'SPIT\Target Type')
    + '" ETYP="' + GetElementEditValues(spel, 'ETYP') + '"');

  effects := ElementByName(spel, 'Effects');
  if not Assigned(effects) then begin Problem('no Effects container in PowerCampfire'); Exit; end;
  while ElementCount(effects) > 0 do
    RemoveByIndex(effects, 0, True);
  e := ElementAssign(effects, HighInteger, nil, False);
  if not Assigned(e) then begin Problem('effect not added to PowerCampfire'); Exit; end;
  PutNative(e, 'EFID', GetLoadOrderFormID(mgef));
  PutNative(e, 'EFIT\Magnitude', 0.0);
  PutNative(e, 'EFIT\Area',      0);
  PutNative(e, 'EFIT\Duration',  0);   // Constant Effect - ignored; script Dispel()s
  Remember(PFX + 'PowerCampfire', spel);

  // notifications (feature 5/6/7)
  // Wetness is the one axis with no bar and no icon. Two lines in the corner
  // are the whole of what the player ever sees of it.
  AddMsg(PFX + 'MsgWetSoaked',    L('wet.msg.soaked'));
  AddMsg(PFX + 'MsgWetDry',       L('wet.msg.dry'));
  AddMsg(PFX + 'MsgCampLit',      L('msg.camp.lit'));
  AddMsg(PFX + 'MsgCampOut',      L('msg.camp.out'));
  AddMsg(PFX + 'MsgCampNoFuel',   L('msg.camp.nofuel'));
  AddMsg(PFX + 'MsgCampNoPerk',   L('msg.camp.noperk'));
  AddMsg(PFX + 'MsgCampRain',     L('msg.camp.rain'));
  AddMsgConfirm(FindMsgBoxTemplate, PFX + 'MsgCampConfirm',
    L('msg.camp.confirm'), L('phrase.yes'), L('phrase.no'));
  AddMsg(PFX + 'MsgTreeCooldown', L('msg.tree.cooldown'));

  // Shared by the fire and the bedroll: both refuse when the way ahead is
  // solid, and both say the same thing about it.
  AddMsg(PFX + 'MsgNoRoom',       L('msg.place.noroom'));
  AddMsg(PFX + 'MsgNoTeleport',   L('msg.cold.noteleport'));

  Say('  campfire: PowerCampfire (Lesser Power) + MgefLightCampfire + 6 MESG');
end;

// True if the FURN's KWDA holds a keyword whose EditorID contains `kwStem`.
function FurnHasKwdStem(r: IwbMainRecord; kwStem: string): Boolean;
var
  kwda, k: IInterface;
  i: Integer;
begin
  Result := False;
  kwda := ElementByName(r, 'KWDA');
  if not Assigned(kwda) then Exit;
  for i := 0 to Pred(ElementCount(kwda)) do begin
    k := LinksTo(ElementByIndex(kwda, i));
    if Assigned(k) and (Pos(LowerCase(kwStem), LowerCase(EditorID(k))) > 0) then begin
      Result := True;
      Exit;
    end;
  end;
end;

// A Skyrim.esm bedroll FURN to copy (furniture markers + sleep keyword carry
// over). Tries: EditorID contains "bedroll"; then KWDA has a "bedroll" keyword;
// then EditorID contains "bedroll"/"bed" + a FurnitureBed* keyword. Logs the
// 'bed' FURN it sees so a miss can be diagnosed.
function FindBedrollFurn: IwbMainRecord;
var
  grp: IwbGroupRecord;
  r  : IwbMainRecord;
  i, seen: Integer;
  ed : string;
begin
  Result := nil;
  grp := GroupBySignature(FileByName('Skyrim.esm'), 'FURN');
  if not Assigned(grp) then Exit;
  seen := 0;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    ed := LowerCase(EditorID(r));
    if Pos('bed', ed) > 0 then begin
      if seen < 20 then Say('    FURN bed?: ' + EditorID(r));
      Inc(seen);
    end;
    if Pos('bedroll', ed) > 0 then begin
      Result := r;
      Say('  bedroll FURN template (edid): ' + EditorID(r));
      Exit;
    end;
  end;
  // second pass: any FURN whose keywords say bedroll / furniture-bed
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if FurnHasKwdStem(r, 'bedroll') then begin
      Result := r;
      Say('  bedroll FURN template (kwd bedroll): ' + EditorID(r));
      Exit;
    end;
  end;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if (Pos('bed', LowerCase(EditorID(r))) > 0) and FurnHasKwdStem(r, 'furniturebed') then begin
      Result := r;
      Say('  bedroll FURN template (bed + kwd): ' + EditorID(r));
      Exit;
    end;
  end;
  Problem('no bedroll FURN found in Skyrim.esm - see the "FURN bed?" list above');
end;

// A Skyrim.esm tanning-rack recipe to use as a COBJ template. Prefer
// RecipeLeather01, else the first COBJ whose workbench keyword is
// CraftingTanningRack.
function FindTanningRecipe: IwbMainRecord;
var
  grp: IwbGroupRecord;
  r, kwd: IwbMainRecord;
  i  : Integer;
begin
  Result := RecordByEDID(FileByName('Skyrim.esm'), 'COBJ', 'RecipeLeather01');
  if Assigned(Result) then Exit;
  kwd := RecordByEDID(FileByName('Skyrim.esm'), 'KYWD', 'CraftingTanningRack');
  if not Assigned(kwd) then Exit;
  grp := GroupBySignature(FileByName('Skyrim.esm'), 'COBJ');
  if not Assigned(grp) then Exit;
  for i := 0 to Pred(ElementCount(grp)) do begin
    r := ElementByIndex(grp, i);
    if GetNativeValue(ElementByPath(r, 'BNAM')) = GetLoadOrderFormID(kwd) then begin
      Result := r;
      Say('  tanning recipe template: ' + EditorID(r));
      Exit;
    end;
  end;
end;

// Portable bedroll (feature 8): _RSL_BedrollItem MISC (craft at a tanning rack)
// <-> _RSL_BedrollFurn FURN (drop to place, grab to reclaim). Vanilla assets,
// scripts are our own. See _RSL_BedrollItem.psc for the pattern note.
procedure BuildBedroll;
var
  miscTpl, furnTpl, cobjTpl, mi, fu, co, kwd, leather, perk: IwbMainRecord;
  items, ci, cond: IInterface;
begin
  Say('');
  Say('--- portable bedroll ---');

  // MISC item - template Firewood01 (a plain, weightless-ish MISC)
  mi := RecordByEDID(tgt, 'MISC', PFX + 'BedrollItem');
  if Assigned(mi) then begin
    ScrubTemplate(mi); Inc(reused);
  end else begin
    miscTpl := RecordByEDID(FileByName('Skyrim.esm'), 'MISC', 'Firewood01');
    if not Assigned(miscTpl) then begin Problem('Firewood01 MISC template missing'); Exit; end;
    mi := wbCopyElementToFile(miscTpl, tgt, True, True);
    if not Assigned(mi) then begin Problem('BedrollItem MISC not copied'); Exit; end;
    ScrubTemplate(mi);
    PutEdit(mi, 'EDID', PFX + 'BedrollItem');
    Inc(madeNew);
  end;
  PutEdit(mi, 'FULL', L('bedroll.full'));
  PutEdit(mi, 'Model\MODL', 'Furniture\Bedroll\Bedroll01.nif');
  PutNative(mi, 'DATA\Value',  25);
  PutNative(mi, 'DATA\Weight', 4.0);
  // No script. Dropping it is a TESContainerChangedEvent in the native core
  // (Core/Events.cpp -> Bedroll::Place).
  DropElement(mi, 'VMAD');
  Remember(PFX + 'BedrollItem', mi);

  // FURN - a vanilla bedroll furniture, re-modelled to Bedroll01
  fu := RecordByEDID(tgt, 'FURN', PFX + 'BedrollFurn');
  if Assigned(fu) then begin
    Inc(reused);
  end else begin
    furnTpl := FindBedrollFurn;
    if not Assigned(furnTpl) then Exit;
    fu := wbCopyElementToFile(furnTpl, tgt, True, True);
    if not Assigned(fu) then begin Problem('BedrollFurn FURN not copied'); Exit; end;
    PutEdit(fu, 'EDID', PFX + 'BedrollFurn');
    Inc(madeNew);
  end;
  DropElement(fu, 'VMAD');
  PutEdit(fu, 'FULL', L('bedroll.full'));
  PutEdit(fu, 'Model\MODL', 'Furniture\Bedroll\Bedroll01.nif');
  // No script. Picking it back up is Bedroll::Update watching the grab.
  // (DropElement above already ran on the copied template.)
  Remember(PFX + 'BedrollFurn', fu);

  // COBJ - tanning rack recipe: 4 Leather01 -> 1 BedrollItem
  co := RecordByEDID(tgt, 'COBJ', PFX + 'RecipeBedroll');
  if Assigned(co) then begin
    Inc(reused);
  end else begin
    cobjTpl := FindTanningRecipe;
    if not Assigned(cobjTpl) then begin Problem('no tanning-rack COBJ template found'); Exit; end;
    co := wbCopyElementToFile(cobjTpl, tgt, True, True);
    if not Assigned(co) then begin Problem('RecipeBedroll COBJ not copied'); Exit; end;
    PutEdit(co, 'EDID', PFX + 'RecipeBedroll');
    Inc(madeNew);
  end;
  // Wipe the template's own conditions (RecipeLeather01 needs an AnimalHide),
  // then put ours back: the bedroll is gated on the same perk the campfire is.
  // The container is kept rather than dropped - AddCond wants one.
  cond := ElementByName(co, 'Conditions');
  if Assigned(cond) then
    while ElementCount(cond) > 0 do RemoveByIndex(cond, 0, True);

  perk := RecordByEDID(FileByName('RFAB.esp'), 'PERK',
    'RFAB_Perk_Survival_BaseSurvival');
  if not Assigned(perk) then begin
    Problem('RFAB_Perk_Survival_BaseSurvival не найден - рецепт спальника без перка');
  end else
    AddCond(co, CTDA_FUNC_HASPERK, CTDA_OP_EQ, 1.0, perk);

  PutNative(co, 'CNAM', GetLoadOrderFormID(mi));   // created object -> our item
  PutNative(co, 'NAM1', 1);

  // component: Leather01 x4 (resolve by EditorID, not a hardcoded FormID)
  leather := RecordByEDID(FileByName('Skyrim.esm'), 'MISC', 'Leather01');
  if not Assigned(leather) then leather := RecordByEDID(FileByName('Skyrim.esm'), 'MISC', 'Leather');
  items := ElementByName(co, 'Items');
  if Assigned(items) and Assigned(leather) then begin
    while ElementCount(items) > 0 do RemoveByIndex(items, 0, True);
    ci := ElementAssign(items, HighInteger, nil, False);
    PutNative(ci, 'CNTO\Item',  GetLoadOrderFormID(leather));
    PutNative(ci, 'CNTO\Count', 4);
    PutNative(co, 'COCT', 1);
  end else
    Problem('RecipeBedroll: no Items container / Leather01 not found');

  // workbench keyword
  kwd := RecordByEDID(FileByName('Skyrim.esm'), 'KYWD', 'CraftingTanningRack');
  if Assigned(kwd) then
    PutNative(co, 'BNAM', GetLoadOrderFormID(kwd));
  Remember(PFX + 'RecipeBedroll', co);

  Say('  bedroll COBJ: BNAM="' + GetElementEditValues(co, 'BNAM')
    + '" CNAM="' + GetElementEditValues(co, 'CNAM')
    + '" NAM1=' + GetElementEditValues(co, 'NAM1')
    + ' COCT=' + GetElementEditValues(co, 'COCT')
    + ' comp="' + GetElementEditValues(co, 'Items\[0]\CNTO\Item')
    + '" x' + GetElementEditValues(co, 'Items\[0]\CNTO\Count'));
  Say('  bedroll: MISC ' + IntToHex(LocalID(mi), 6) + ' / FURN ' + IntToHex(LocalID(fu), 6)
    + ' / COBJ ' + IntToHex(LocalID(co), 6));
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

  // _RSL_Controller is NOT bound any more. The native core replaced it, and
  // while both were bound the two ran side by side: two survival mods on one
  // player. That is what put camera effects and the frost shader on screen
  // after a night in a bed with full needs, with nothing in _RSL_Core.log to
  // show for it - the native side implements no image-space modifiers at all,
  // so it could not have been the one doing it.
  //
  // Worse, the MCM toggle only ever reached the Papyrus side: it writes the
  // _RSL_ModEnabled GLOB, and the native core reads settings.ini and never
  // looks at a GLOB. So "mod off" switched off the OLD core and left the new
  // one running - the exact opposite of what it says.
  //
  // ScrubTemplate above already drops VMAD, so not re-attaching here is what
  // removes the binding on a regenerated record.

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

  // Not bound, for the same reason as _RSL_Controller: the ability it exists
  // to hand out carries a script that no longer does anything. The quest and
  // the ability records stay - dropping a record breaks every save that has
  // it, and an empty Start Game Enabled quest costs nothing.
  DropElement(qst, 'VMAD');
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

// HUD widget quest. Empty, and kept only so that saves made while the SkyUI
// widget existed still resolve the record.
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

  // Nothing is bound and nothing loads: the widget is the native menu's own
  // SWF now, and both the SkyUI widget script and the SWF it loaded are gone
  // from the mod. VMAD is dropped so a plugin from an older run stops binding
  // a script that no longer exists.
  DropElement(qst, 'VMAD');

  // The quest record stays: dropping a record breaks every save that has it,
  // and an empty Start Game Enabled quest costs nothing.
end;

// Emit native\src\Core\FormIDs.h - every formID this run created, as C++
// constants.
//
// Before this existed the native plugin carried its ids as hand-typed
// constexprs, which was a standing trap: CopyVanillaMgef drops and re-copies
// each library MGEF on every run, so ids churn, and nothing connected the two
// files. A regenerated plugin could silently stop matching the DLL. It also
// meant every new record needed a manual round trip - run the generator, read
// the ids back, retype them - which is exactly the sort of step that gets done
// wrong once and then believed.
//
// Names are the EditorID with the mod prefix stripped, so _RSL_AbHypo1 becomes
// FormIDs::AbHypo1.
procedure WriteFormIdHeader;
var
  sl  : TStringList;
  path, edid, name: string;
  i, p: Integer;
begin
  Say('');
  Say('--- emit FormIDs.h ---');
  sl := TStringList.Create;
  try
    sl.Add('#pragma once');
    sl.Add('');
    sl.Add('// AUTO-GENERATED by SSEEdit_Scripts\RFAB_SurvivalLayer_01_Records.pas');
    sl.Add('//');
    sl.Add('// DO NOT EDIT BY HAND -- the next generator run overwrites this file.');
    sl.Add('//');
    sl.Add('// Local formIDs of every record the generator built, so the plugin and the');
    sl.Add('// .esp cannot drift apart. They do churn: CopyVanillaMgef drops and re-copies');
    sl.Add('// the penalty library on every run.');
    sl.Add('');
    sl.Add('namespace RSL::FormIDs');
    sl.Add('{');
    for i := 0 to Pred(ids.Count) do begin
      edid := ids.Names[i];
      if edid = '' then Continue;
      name := edid;
      if Copy(name, 1, Length(PFX)) = PFX then
        name := Copy(name, Length(PFX) + 1, Length(name));
      sl.Add('    inline constexpr RE::FormID ' + name
           + ' = 0x' + ids.Values[edid] + ';');
    end;
    sl.Add('}');
    sl.Add('');

    path := MOD_DIR + 'native\src\Core\FormIDs.h';
    sl.SaveToFile(path);
    Say('  written: ' + path + ' (' + IntToStr(ids.Count) + ' ids)');
    Say('  REBUILD the native plugin after this.');
  finally
    sl.Free;
  end;
end;

// --- v0.4.0: overrides on RFAB.esp own records -----------------------------
//
// Unlike everything above, these are OVERRIDES, not new records:
// wbCopyElementToFile with AsNew=False keeps RFAB's formID, so the game sees
// the retuned original rather than a duplicate. Our plugin already masters
// RFAB.esp and must load after it. Reruns are safe - copying a record that is
// already overridden returns the existing override.

// aAsNew=False keeps the master's formID; aDeepCopy=TRUE is what actually
// brings the subrecords along. With False the override comes out as a bare
// record header - every field empty - and then editing it does not fail, it
// silently ships a blanked-out version of RFAB's record.
//
// Any override left by an earlier run is dropped first and re-derived from the
// master: reusing one would inherit whatever that run left behind (including a
// blanked record), and every edit below is a fresh re-derivation anyway.
function OverrideOf(src: IwbMainRecord): IwbMainRecord;
var
  old: IwbMainRecord;
begin
  Result := nil;
  if not Assigned(src) then Exit;
  AddMasterIfMissing(tgt, GetFileName(GetFile(MasterOrSelf(src))));
  // ...and the files the record's own fields POINT AT, which is a different
  // list. HelpManualPC taught it: entry 24 of RFAB's 45 is a MESG from
  // Update.esm, which was never a master here, so that one entry could not be
  // mapped and the copy lost it. This is xEdit's own answer - its shipped
  // "Copy as override.pas" calls exactly this before every wbCopyElementToFile.
  AddRequiredElementMasters(src, tgt, False);

  old := RecordByEDID(tgt, Signature(src), EditorID(src));
  if Assigned(old) then begin
    Say('    re-deriving override of ' + EditorID(src) + ' from the master');
    Remove(old);
  end;

  Result := wbCopyElementToFile(src, tgt, False, True);
  if not Assigned(Result) then
    Problem('override not created for ' + EditorID(src));
end;

// Set a native value on a named field of a container.
//
// Two traps here, both paid for once already:
//   - the compound path 'Magic Effect Data\DATA\Minimum Skill Level' resolves
//     to nil even though the field plainly exists (the same JvInterpreter quirk
//     MgefFlags documents for DATA\Flags), so the container is resolved first
//     and the field looked up by name on it;
//   - TStringList.CommaText splits on SPACES as well as commas - see line 660,
//     which converts spaces to commas on purpose - so a list of names with
//     spaces in them cannot be passed that way. One name, no splitting.
//
// A miss logs what the container actually holds, which is how these two names
// were pinned down in the first place.
function PutNativeIn(rec: IwbMainRecord; containerPath, fieldName: string; value: Variant): Boolean;
var
  cont, el: IInterface;
  i   : Integer;
  have: string;
begin
  Result := False;

  cont := ElementByPath(rec, containerPath);
  if not Assigned(cont) then begin
    Problem('нет "' + containerPath + '" в ' + EditorID(rec));
    Exit;
  end;

  el := ElementByName(cont, fieldName);
  if not Assigned(el) then begin
    have := '';
    for i := 0 to Pred(ElementCount(cont)) do
      have := have + ' | ' + Name(ElementByIndex(cont, i));
    Problem('"' + fieldName + '" не найдено в ' + containerPath + ' записи '
          + EditorID(rec) + '. Есть:' + have);
    Exit;
  end;

  SetNativeValue(el, value);
  Result := True;
end;

function RfabRec(sig, edid: string): IwbMainRecord;
begin
  Result := RecordByEDID(FileByName('RFAB.esp'), sig, edid);
  if not Assigned(Result) then
    Problem(sig + ' ' + edid + ' не найден в RFAB.esp');
end;

// Our page in the game's own Help section (Journal -> Help).
//
// HOW THAT SECTION IS BUILT, measured rather than assumed: it is a FormList.
// Skyrim's HelpManualPC (00000163) holds 42 MESG records, each one an entry -
// FULL is the line in the list, DESC is the page behind it.
//
// RFAB does TWO different things to it, and they are easy to confuse. It
// renames 22 vanilla entries (HelpAlchemyLong and the rest gain an "[RFAB]"
// prefix), which adds nothing; and it appends three of its own, so its
// HelpManualPC holds 45.
//
// WHICH IS WHY THE OVERRIDE IS TAKEN FROM RFAB AND NOT FROM THE MASTER. Copying
// Skyrim's 42-entry version and appending ours would silently drop RFAB's three
// - the same trap CureDiseaseMsg already taught this file. RfabRec hands us
// their version; OverrideOf copies whatever it is given.
//
// HelpManualXBox (00000165) is the gamepad twin and is deliberately left alone.
// The cough: one sound descriptor per FILE, twenty-three of them.
//
// WHY ONE PER FILE, and not one per voice with a list inside it. A SNDR can
// hold a LIST of files (ANAM repeats) and the engine picks from it, which is
// what this was written against first. It cannot be built from a script:
//
//   - xEdit refuses to ADD an entry to the 'Sound Files' array. Assigning into
//     the container hands back the array itself, which carries no value of its
//     own - the "Sound Files can not be edited" the first run died on - and a
//     run that reached the array directly wrote nothing while reporting four
//     tidy successes.
//   - However many files a record really holds, the array reached from a
//     script offers exactly ONE editable slot. Three templates, one rule:
//     NPCDogIdleWhine (16 files in the saved record) counted 1 and tripped the
//     check below; NPCWerewolfBreatheOut (2) counted 1, so the old code's
//     "remove what the template brought" removed one file and left the other;
//     NPCHumanCartExitA (1) counts 1 and comes out exactly right.
//
// So the template has to hold exactly one file, and each of our files needs a
// record of its own. Why the array behaves this way is not established here -
// what is established is that it does, on every template tried.
//
// Writing the value of the one slot that IS there works - xEdit's own
// "Read Books Aloud" sets 'Sounds\Sound Files\ANAM - File Name' on a copied
// SNDR exactly that way. So each file gets its own descriptor and the PLUGIN
// picks between them, which costs nothing: the lists per voice live in
// Cough.cpp, and the male files are shared by the orc and khajiit lists by
// reference instead of being duplicated into three separate records.
//
// The template is NPCHumanCartExitA: a single-file human NPC sound whose
// CNAM, GNAM (AudioCategorySFX), ONAM and LNAM are identical to the
// NPCWerewolfBreatheOut this used to clone. Only BNAM differs - it is quieter
// by design, and loudness is set from the plugin anyway.
//
// The files are 16-bit mono PCM at 44.1 kHz. They arrive as 24-bit stereo and
// have to be converted - Skyrim plays neither 24-bit nor, positionally, stereo.
procedure BuildCoughSound(edid, wav: string);
var
  src, rec       : IwbMainRecord;
  anams, snd, el : IInterface;
  i              : Integer;
  fresh          : Boolean;
  have, want, diag : string;
begin
  want := 'Data\Sound\FX\RSL\Cough\' + wav;

  src := RecordByEDID(FileByName('Skyrim.esm'), 'SNDR', 'NPCHumanCartExitA');
  if not Assigned(src) then begin
    Problem('BuildCoughSound: шаблон NPCHumanCartExitA не найден');
    Exit;
  end;
  // A new record, but its category, output model and conditions still point
  // into Skyrim.esm - the same call, for the same reason, as OverrideOf.
  AddRequiredElementMasters(src, tgt, False);

  // KEEP THE FORMID, same reason as the rest: these are referenced from
  // native/src/Core/FormIDs.h, and a released build's ids must not move under
  // a player's save. Nothing else of the template is needed on a reuse - the
  // category, output model and conditions are already on the record, and the
  // one field that matters is rewritten and read back below.
  rec := RecordByEDID(tgt, 'SNDR', edid);
  fresh := not Assigned(rec);
  if fresh then begin
    rec := wbCopyElementToFile(src, tgt, True, True);
    if not Assigned(rec) then begin
      Problem('BuildCoughSound: SNDR не скопирован ' + edid);
      Exit;
    end;
  end;
  PutEdit(rec, 'EDID', edid);

  // Resolved in two steps, not as one compound path: PutNativeIn documents
  // above why a multi-level path can come back nil where the plain lookup on
  // the container works. A miss prints what the record really holds.
  snd := ElementByName(rec, 'Sounds');
  anams := nil;
  if Assigned(snd) then anams := ElementByName(snd, 'Sound Files');
  if not Assigned(anams) then begin
    if not Assigned(snd) then snd := rec;
    diag := '';
    for i := 0 to Pred(ElementCount(snd)) do
      diag := diag + ' | ' + Name(ElementByIndex(snd, i));
    Problem('BuildCoughSound: нет массива Sound Files в ' + edid + '. Есть:' + diag);
    Exit;
  end;
  if ElementCount(anams) <> 1 then begin
    Problem('BuildCoughSound: в копии ' + IntToStr(ElementCount(anams))
      + ' слот(ов) вместо одного (' + edid + ')');
    Exit;
  end;

  el := ElementByIndex(anams, 0);
  SetEditValue(el, want);

  // Read back. An earlier shape of this reported four tidy successes and left
  // the template's own file in place, so nothing here is taken on trust.
  have := GetEditValue(el);
  if not SameText(have, want) then begin
    Problem(edid + ': записалось "' + have + '", ожидалось "' + want + '"');
    Exit;
  end;

  Remember(edid, rec);
  if fresh then Inc(madeNew) else Inc(reused);
end;

procedure BuildCoughSounds;
var
  i, n : Integer;
begin
  Say('');
  Say('--- SNDR: кашель ---');
  n := madeNew;
  for i := 1 to 9 do
    BuildCoughSound(PFX + 'SndCoughFemale' + IntToStr(i),
      'female_cough_' + IntToStr(i) + '.wav');
  for i := 1 to 8 do
    BuildCoughSound(PFX + 'SndCoughMale' + IntToStr(i),
      'male_cough_' + IntToStr(i) + '.wav');
  for i := 1 to 4 do
    BuildCoughSound(PFX + 'SndCoughOrc' + IntToStr(i),
      'orc_male_cough_' + IntToStr(i) + '.wav');
  for i := 1 to 2 do
    BuildCoughSound(PFX + 'SndCoughKhajiit' + IntToStr(i),
      'khajiit_male_cough_' + IntToStr(i) + '.wav');
  Say('  дескрипторов: ' + IntToStr(madeNew - n) + ' из 23 (по одному файлу в каждом)');
end;

procedure BuildHelpTopic;
var
  msg, src, flst : IwbMainRecord;
  items          : IInterface;
begin
  Say('');
  Say('--- help topic ---');

  msg := AddMsg(PFX + 'MsgHelpSurvival', Multiline(L('help.survival.body')));
  if not Assigned(msg) then Exit;
  // A help entry needs both halves: AddMsg writes only the body.
  PutEdit(msg, 'FULL', L('help.survival.title'));

  src := RfabRec('FLST', 'HelpManualPC');
  if not Assigned(src) then Exit;

  flst := OverrideOf(src);
  if not Assigned(flst) then Exit;

  items := ElementByName(flst, 'FormIDs');
  if not Assigned(items) then begin
    Problem('HelpManualPC: нет контейнера FormIDs');
    Exit;
  end;
  FlstAddRecord(items, msg);
  FlstRequireResolved(items, 'HelpManualPC');
  Say('  HelpManualPC: +1 запись, теперь ' + IntToStr(ElementCount(items)));
end;


// WB_A100_ControlWeather_Effect is flagged A100 by its own EditorID, but it
// still carries Minimum Skill Level 0 and the spell still points at the novice
// half-cost perk - so the game lists it as a novice spell. Fix both; cost and
// charge time stay as RFAB set them.
procedure PatchControlWeather;
var
  mgef, spel, masterPerk, ovr: IwbMainRecord;
begin
  Say('');
  Say('--- RFAB override: "Управление погодой" -> мастер ---');

  masterPerk := RecordByEDID(FileByName('Skyrim.esm'), 'PERK', 'AlterationMaster100');
  if not Assigned(masterPerk) then begin
    Problem('AlterationMaster100 не найден в Skyrim.esm');
    Exit;
  end;

  mgef := RfabRec('MGEF', 'WB_A100_ControlWeather_Effect');
  if Assigned(mgef) then begin
    ovr := OverrideOf(mgef);
    if Assigned(ovr) then begin
      if PutNativeIn(ovr, 'Magic Effect Data\DATA', 'Minimum Skill Level', 100) then
        Say('  MGEF WB_A100_ControlWeather_Effect: Minimum Skill Level = 100');
    end;
  end;

  // The field is "Half-cost Perk" in this xEdit build, not "Casting Perk".
  // RFAB overrides the vanilla alteration perks in place, so 000C44BA resolves
  // to its own RFAB_Perk_Alteration_MasterAlteration at runtime.
  spel := RfabRec('SPEL', 'RFAB_Spell_Alteration1_ControlWeather_PC');
  if Assigned(spel) then begin
    ovr := OverrideOf(spel);
    if Assigned(ovr) then begin
      if PutNativeIn(ovr, 'SPIT', 'Half-cost Perk', GetLoadOrderFormID(masterPerk)) then
        Say('  SPEL RFAB_Spell_Alteration1_ControlWeather_PC: '
          + 'Half-cost Perk = AlterationMaster100');
    end;
  end;
end;

// v0.4.0. Everything this layer appends to an RFAB perk description starts
// here, and a rerun cuts the description back to this seam before appending
// again, so the text can never be doubled up.
//
// It used to be a const in this file, which made it the one piece of text the
// player reads that did not live with the rest of it. The <br> stays here -
// that is markup, not text.
function PerkMark: string;
begin
  Result := '<br>' + L('perk.mark') + '<br>';
end;

// Append this layer's own line to an RFAB perk description. Additive and
// idempotent: everything from the marker on is cut first, so the original RFAB
// text is preserved and a rerun replaces our block instead of stacking copies.
procedure AppendPerkNote(edid, strKey: string);
var
  perk, ovr: IwbMainRecord;
  desc, note: string;
  cut: Integer;
begin
  // A missing key must not reach the record: L() would hand back the key name
  // itself and that is what would end up in the perk description in game.
  if not HasStr(strKey) then begin
    Problem('нет строки ' + strKey + ' - описание ' + edid + ' не тронуто'
          + ' (не забыт ли deploy.sh?)');
    Exit;
  end;
  if not HasStr('perk.mark') then begin
    Problem('нет строки perk.mark - описание ' + edid + ' не тронуто');
    Exit;
  end;
  note := Trim(L(strKey));
  if note = '' then begin
    Say('  ' + edid + ': ключ ' + strKey + ' пуст - описание не тронуто');
    Exit;
  end;

  perk := RfabRec('PERK', edid);
  if not Assigned(perk) then Exit;

  ovr := OverrideOf(perk);
  if not Assigned(ovr) then Exit;

  desc := GetElementEditValues(ovr, 'DESC');
  cut := Pos(PerkMark, desc);
  if cut > 0 then
    desc := Copy(desc, 1, cut - 1);

  if PutEdit(ovr, 'DESC', desc + PerkMark + note) then
    Say('  ' + edid + ': описание дополнено (' + strKey + ')');
end;

procedure PatchRfabPerks;
begin
  Say('');
  Say('--- RFAB override: описания перков выживания ---');
  AppendPerkNote('RFAB_Perk_Survival_BaseSurvival',    'perk.BaseSurvival.add');
  AppendPerkNote('RFAB_Perk_Survival_Acclimatization', 'perk.Acclimatization.add');
  AppendPerkNote('RFAB_Perk_Survival_Chef',            'perk.Chef.add');
  AppendPerkNote('RFAB_Perk_Survival_Cheerfulness',    'perk.Cheerfulness.add');
end;

// ---------------------------------------------------------------------------

function Initialize: Integer;
begin
  Result   := 0;
  problems := 0;
  madeNew  := 0;
  reused   := 0;
  ids      := TStringList.Create;

  Say('============================================================');
  Say(' RFAB Survival Layer -- генератор записей, часть 1');
  Say('============================================================');

  LoadStrings;

  tgt := EnsureTargetFile;
  if not Assigned(tgt) then begin
    Result := 1;
    Exit;
  end;

  BuildFireList;
  BuildColdInteriors;
  BuildEffectsAndSpells;
  BuildPenaltyLib;
  BuildBonusAbility;
  BuildCampfire;
  BuildBedroll;
  BuildWaterRecipe;
  BuildDiseases;
  BuildRfabWrappers;
  BuildHypothermia;
  BuildHelpTopic;
  BuildCoughSounds;
  PatchControlWeather;
  PatchRfabPerks;
  PurgeStaleRecords;
  BuildMonitorAndQuest;
  BuildMcmQuest;
  BuildWidgetQuest;
  WriteFormIdHeader;

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
  Say('  3. Пересобрать нативный плагин: FormIDs.h только что перегенерирован,');
  Say('     без пересборки DLL будет смотреть на старые formID.');
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
  if Assigned(strTbl) then
    strTbl.Free;
  Result := 0;
end;

end.
