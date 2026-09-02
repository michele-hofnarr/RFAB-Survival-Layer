{
  RFAB Survival Layer - dependency validator.

  Apply Script against the active load order (any file selected). Prints a
  PASS/FAIL report for every external record RFAB Survival Layer resolves at
  runtime (baked into scripts/source/_RSL_Forms.psc as GetFormFromFile calls,
  or looked up by EditorID by the generator).

  Run this after updating RFAB to see, in one pass, which dependencies moved
  or were renamed. FAIL on a FormID line = that local id no longer holds the
  expected kind of record in that file (RFAB reindexed / removed it). A name
  change alone still PASSES but prints the new EditorID - eyeball the list.

  The dependency table is DepTable below - one edit to update it.
}
unit RFAB_Validate_Deps;

const
  SELF_PLUGIN = 'RFAB_SurvivalLayer.esp';

var
  passN, failN, warnN: Integer;

procedure Line(s: string);
begin
  AddMessage(s);
end;

// Every dependency, one per line:  file | key | SIG | note
//   key '#<hex6>' -> match by local FormID (the value _RSL_Forms.psc hardcodes)
//   key '<edid>'  -> match by EditorID (how the generator resolves templates)
procedure DepTable(sl: TStringList);
begin
  // --- vanilla MGEF the penalty library deep-copies (generator, by EditorID) ---
  sl.Add('Skyrim.esm|AbDamageMagickaRate|MGEF|regen -%: magicka');
  sl.Add('Skyrim.esm|AbDamageStaminaRateVisible|MGEF|regen -%: stamina');
  sl.Add('Skyrim.esm|AbDamageHealRateVisible|MGEF|regen -%: health');
  sl.Add('Skyrim.esm|AlchDamageSpeed|MGEF|move speed (Peak Value Mod)');
  sl.Add('Skyrim.esm|AbFortifySneak|MGEF|sneak (flip Detrimental)');
  sl.Add('Skyrim.esm|AbResistMagic|MGEF|magic weakness (flip)');
  sl.Add('Skyrim.esm|AbFortifyCarryWeight|MGEF|carry weight (flip)');
  sl.Add('Skyrim.esm|AbFortifyHealRate|MGEF|health drain (flip)');
  sl.Add('Skyrim.esm|BladesAbBlessing|MGEF|max health (flip)');
  sl.Add('Skyrim.esm|AlchFortifyMagicka|MGEF|max magicka (flip)');
  sl.Add('Skyrim.esm|AlchFortifyStamina|MGEF|max stamina (flip)');
  sl.Add('Skyrim.esm|MG02FortifyAlteration|MGEF|school cost: alteration');
  sl.Add('Skyrim.esm|MG02FortifyConjuration|MGEF|school cost: conjuration');
  sl.Add('Skyrim.esm|MG02FortifyDestruction|MGEF|school cost: destruction');
  sl.Add('Skyrim.esm|MG02FortifyIllusion|MGEF|school cost: illusion');
  sl.Add('Skyrim.esm|MG02FortifyRestoration|MGEF|school cost: restoration');
  sl.Add('Skyrim.esm|AbResistFrost|MGEF|frost-resist bonus (wrapper st.2/3)');
  sl.Add('RFAB.esp|RFAB_Effect_PeryiteWitbane_DecreaseAttackSpeed|MGEF|attack speed');
  sl.Add('RFAB.esp|RFAB_Effect_PeryiteWitbane_WeaknessCastSpeed|MGEF|cast speed');
  sl.Add('RFAB.esp|RFAB_Effect_PeryiteAtaxia_ResistStagger_Hide|MGEF|poise / poise bonus');
  sl.Add('RFAB.esp|RFAB_Effect_PeryiteRockjoint_FortifyArmorRating|MGEF|armor bonus');

  // --- RFAB / DLC disease SPELs the wrappers wrap (generator, by EditorID) ---
  sl.Add('RFAB.esp|RFAB_Disease_Ataxia|SPEL|wrapper AT source');
  sl.Add('RFAB.esp|RFAB_Disease_Rockjoint|SPEL|wrapper RJ source');
  sl.Add('RFAB.esp|RFAB_Disease_Witbane|SPEL|wrapper WB source');
  sl.Add('RFAB.esp|RFAB_Disease_Rattles|SPEL|wrapper RA source');
  sl.Add('RFAB.esp|RFAB_Disease_BoneBreakFever|SPEL|wrapper BF source');
  sl.Add('RFAB.esp|RFAB_Disease_BrainRot|SPEL|wrapper BRR source');
  sl.Add('Dragonborn.esm|DLC2DiseaseDroops|SPEL|wrapper DR source');

  // --- hardcoded FormIDs in _RSL_Forms.psc: wrapper stage-1 spells ---
  // (RFAB overrides these vanilla Disease* SPELs; resolved from Skyrim.esm)
  sl.Add('Skyrim.esm|#0B877C|SPEL|RfabDzAT  stage-1 spell');
  sl.Add('Skyrim.esm|#0B8782|SPEL|RfabDzRJ  stage-1 spell');
  sl.Add('Skyrim.esm|#0B8783|SPEL|RfabDzWB  stage-1 spell');
  sl.Add('Skyrim.esm|#0B8781|SPEL|RfabDzRA  stage-1 spell');
  sl.Add('Skyrim.esm|#0B877E|SPEL|RfabDzBF  stage-1 spell');
  sl.Add('Skyrim.esm|#0B877F|SPEL|RfabDzBRR stage-1 spell');
  sl.Add('Dragonborn.esm|#0285C1|SPEL|RfabDzDR stage-1 spell');

  // --- wrapper marker MGEF (first unconditional debuff; HasMagicEffect probe) ---
  sl.Add('RFAB.esp|#0CD9BD|MGEF|RfabDzMarkAT');
  sl.Add('Skyrim.esm|#0B877A|MGEF|RfabDzMarkRJ');
  sl.Add('Skyrim.esm|#0B877B|MGEF|RfabDzMarkWB');
  sl.Add('Skyrim.esm|#0B8779|MGEF|RfabDzMarkRA');
  sl.Add('Skyrim.esm|#0B8776|MGEF|RfabDzMarkBF');
  sl.Add('Skyrim.esm|#0B8777|MGEF|RfabDzMarkBRR');
  sl.Add('Dragonborn.esm|#0285C0|MGEF|RfabDzMarkDR');

  // --- cure-disease MGEF (IsCureEffect: a cure = one stage back) ---
  sl.Add('Skyrim.esm|#10E949|MGEF|cure disease (vanilla potion)');
  sl.Add('Skyrim.esm|#0FBFF5|MGEF|cure disease (shrine blessing)');
  sl.Add('Skyrim.esm|#0AE722|MGEF|cure disease (vampirism start)');
  sl.Add('RFAB.esp|#005879|MGEF|cure disease (RFAB)');
  sl.Add('RFAB.esp|#01ED5F|MGEF|cure disease (RFAB)');
  sl.Add('RFAB.esp|#01ED62|MGEF|cure disease (RFAB)');
  sl.Add('RFAB.esp|#0E463F|MGEF|cure disease (RFAB)');

  // --- hold Locations (RegionBase parent-chain match) ---
  sl.Add('Skyrim.esm|#016769|LCTN|hold location');
  sl.Add('Skyrim.esm|#01676A|LCTN|hold location');
  sl.Add('Skyrim.esm|#01676B|LCTN|hold location');
  sl.Add('Skyrim.esm|#01676C|LCTN|hold location');
  sl.Add('Skyrim.esm|#01676D|LCTN|hold location');
  sl.Add('Skyrim.esm|#01676E|LCTN|hold location');
  sl.Add('Skyrim.esm|#01676F|LCTN|hold location');
  sl.Add('Skyrim.esm|#016770|LCTN|hold location');
  sl.Add('Skyrim.esm|#016772|LCTN|hold location');

  // --- races / keywords / shader ---
  sl.Add('Skyrim.esm|#000D53|RACE|RaceDraugr (brown rot on hit)');
  sl.Add('Skyrim.esm|#013203|RACE|RaceSlaughterfish (greenspore on hit)');
  sl.Add('Skyrim.esm|#0F5D16|KYWD|ActorTypeTroll (gutworm on hit)');
  sl.Add('Skyrim.esm|#013796|KYWD|ActorTypeUndead (vampire exemption)');
  sl.Add('Skyrim.esm|MagicDamageFrost|KYWD|frost hit -> cold bar');
  sl.Add('Skyrim.esm|MagicDamageFire|KYWD|fire hit -> cold bar');
  sl.Add('Skyrim.esm|#0DC20D|EFSH|FxColdShader (ice crust visual)');
  sl.Add('RFAB.esp|#0CD63E|KYWD|KwRawFood (food poisoning trigger)');
  sl.Add('RFAB.esp|#4CF31E|KYWD|KwStrongStomach (poison immunity)');
  sl.Add('RFAB.esp|#0CD63D|KYWD|KwSpecialFood (75% hunger restore)');
  sl.Add('RFAB.esp|#0CE2AD|KYWD|KwSpecialDrink (drinks carry no hunger)');
end;

// A record in `sig` group of `fileName` whose EditorID or local FormID matches
// `key`. Reports PASS/FAIL and, on a hit, the record's current EditorID.
procedure CheckDep(fileName, key, sig, note: string);
var
  f    : IwbFile;
  grp  : IwbGroupRecord;
  r, w : IwbMainRecord;
  i    : Integer;
  byFid: Boolean;
  want : string;
  hit  : IwbMainRecord;
begin
  byFid := (Length(key) > 0) and (key[1] = '#');
  if byFid then
    want := UpperCase(Copy(key, 2, Length(key)))
  else
    want := key;

  f := FileByName(fileName);
  if not Assigned(f) then begin
    Inc(failN);
    Line('  FAIL  ' + fileName + ' not in load order  (' + note + ')');
    Exit;
  end;

  grp := GroupBySignature(f, sig);
  if not Assigned(grp) then begin
    Inc(failN);
    Line('  FAIL  ' + fileName + ' has no ' + sig + ' group  (' + note + ')');
    Exit;
  end;

  hit := nil;
  i := 0;
  while (i < ElementCount(grp)) and not Assigned(hit) do begin
    r := ElementByIndex(grp, i);
    if byFid then begin
      if IntToHex(GetLoadOrderFormID(r) and $00FFFFFF, 6) = want then
        hit := r;
    end else begin
      if SameText(EditorID(r), want) then
        hit := r;
    end;
    i := i + 1;
  end;

  if not Assigned(hit) then begin
    Inc(failN);
    if byFid then
      Line('  FAIL  ' + fileName + ' ' + sig + ' #' + want
         + ' - no such record  (' + note + ')')
    else
      Line('  FAIL  ' + fileName + ' ' + sig + ' "' + want
         + '" - not found  (' + note + ')');
    Exit;
  end;

  w := WinningOverride(hit);
  Inc(passN);
  Line('  ok    ' + fileName + ' ' + sig + ' '
     + IntToHex(GetLoadOrderFormID(hit) and $00FFFFFF, 6)
     + ' "' + EditorID(w) + '"  (' + note + ')');
end;

// Assert the archetype templates the generator scans for still exist.
procedure CheckTemplates;
var
  src : IwbFile;
  grp : IwbGroupRecord;
  r   : IwbMainRecord;
  i   : Integer;
  haveVM, haveDisease: Boolean;
begin
  Line('');
  Line('-- template archetypes (generator Find*Template) --');
  src := FileByName('Skyrim.esm');
  if not Assigned(src) then begin
    Inc(failN);
    Line('  FAIL  Skyrim.esm missing');
    Exit;
  end;

  haveVM := False;
  grp := GroupBySignature(src, 'MGEF');
  i := 0;
  while Assigned(grp) and (i < ElementCount(grp)) and not haveVM do begin
    r := ElementByIndex(grp, i);
    if SameText(GetElementEditValues(r, 'Magic Effect Data\DATA\Archtype'), 'Value Modifier')
       and SameText(GetElementEditValues(r, 'Magic Effect Data\DATA\Casting Type'), 'Constant Effect')
       and SameText(GetElementEditValues(r, 'Magic Effect Data\DATA\Delivery'), 'Self') then
      haveVM := True;
    i := i + 1;
  end;
  if haveVM then begin
    Inc(passN); Line('  ok    Value Modifier / Constant Effect / Self MGEF present');
  end else begin
    Inc(failN); Line('  FAIL  no Value Modifier / Constant Effect / Self MGEF');
  end;

  haveDisease := False;
  grp := GroupBySignature(src, 'SPEL');
  i := 0;
  while Assigned(grp) and (i < ElementCount(grp)) and not haveDisease do begin
    r := ElementByIndex(grp, i);
    if SameText(GetElementEditValues(r, 'SPIT\Type'), 'Disease') then
      haveDisease := True;
    i := i + 1;
  end;
  if haveDisease then begin
    Inc(passN); Line('  ok    SPIT\Type = Disease SPEL present');
  end else begin
    Inc(failN); Line('  FAIL  no SPIT\Type = Disease SPEL');
  end;
end;

// Informational: our own record count + a GLOB tally, so a gross mismatch shows.
procedure SelfConsistency;
var
  f       : IwbFile;
  i, n, gl: Integer;
  r       : IwbMainRecord;
begin
  Line('');
  Line('-- self-consistency (informational) --');

  f := FileByName(SELF_PLUGIN);
  if not Assigned(f) then begin
    Inc(warnN);
    Line('  warn  ' + SELF_PLUGIN + ' not in load order - skipping');
    Exit;
  end;

  n := 0;
  gl := 0;
  for i := 0 to Pred(RecordCount(f)) do begin
    r := RecordByIndex(f, i);
    if Pos('_RSL_', EditorID(r)) = 1 then Inc(n);
    if Signature(r) = 'GLOB' then Inc(gl);
  end;
  Line('  _RSL_* records: ' + IntToStr(n) + '  (of which GLOB: ' + IntToStr(gl) + ')');
  Line('  cross-check GLOB count against _RSL_Balance.ResetDefaults SetValue lines.');
end;

function Initialize: Integer;
var
  deps: TStringList;
  i, p1, p2, p3: Integer;
  ln, fn, key, sig, note: string;
begin
  Result := 0;
  passN := 0; failN := 0; warnN := 0;

  Line('============================================================');
  Line(' RFAB Survival Layer - dependency validation');
  Line('============================================================');
  Line('');
  Line('-- external records --');

  deps := TStringList.Create;
  try
    DepTable(deps);
    for i := 0 to Pred(deps.Count) do begin
      ln := deps[i];
      p1 := Pos('|', ln);
      if p1 = 0 then Continue;
      fn := Copy(ln, 1, p1 - 1);
      ln := Copy(ln, p1 + 1, Length(ln));
      p2 := Pos('|', ln);
      key := Copy(ln, 1, p2 - 1);
      ln := Copy(ln, p2 + 1, Length(ln));
      p3 := Pos('|', ln);
      sig  := Copy(ln, 1, p3 - 1);
      note := Copy(ln, p3 + 1, Length(ln));
      CheckDep(fn, key, sig, note);
    end;
  finally
    deps.Free;
  end;

  CheckTemplates;
  SelfConsistency;

  Line('');
  Line('============================================================');
  Line(' PASS ' + IntToStr(passN) + '   FAIL ' + IntToStr(failN)
     + '   WARN ' + IntToStr(warnN));
  if failN = 0 then
    Line(' All dependencies resolve.')
  else
    Line(' SOME DEPENDENCIES MOVED - see FAIL lines above.');
  Line('============================================================');
end;

function Process(e: IInterface): Integer;
begin
  Result := 0;
end;

function Finalize: Integer;
begin
  Result := 0;
end;

end.
