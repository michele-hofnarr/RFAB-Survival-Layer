{
  Prep-1 audit for the disease work. Read-only, not shipped.
  Run: right-click ANY plugin -> Apply Script -> RFAB_Audit_Diseases
  (records are looked up across all loaded masters; target file is ignored).

  Dumps:
   1. the 6 vanilla TrapDiseaseXXX spells + Dragonborn "Droops" candidates -
      winning override, SPIT, every effect (MGEF, EFIT, conditions), MGEF
      archetype/AV/keywords/conditions.
   2. anything with peryite / blessing / affliction / inocul / tincture in the
      EditorID (MGEF dumped in full, incl. conditions).
   3. candidate races (slaughterfish / draugr / troll / orc / khajiit / bosmer
      / argonian) with formID + keywords.
   4. keyword / formlist / global whose EditorID looks like a raw-food or
      strong-stomach anchor.
   5. ALCH marked VendorItemFoodRaw or with "raw" in the EditorID.
   6. MGEF/SPEL whose EditorID looks like a heal-rate / regen toggle.
}
unit RFAB_Audit_Diseases;

procedure Say(s: string);
begin
  AddMessage(s);
end;

function LC(s: string): string;
begin
  Result := LowerCase(s);
end;

function Has(s, sub: string): Boolean;
begin
  Result := Pos(sub, LC(s)) > 0;
end;

// First record with this EDID in any loaded file, then its winning override.
function FindWinning(sig, edid: string): IwbMainRecord;
var
  i, j: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
begin
  Result := nil;
  for i := 0 to Pred(FileCount) do begin
    grp := GroupBySignature(FileByIndex(i), sig);
    if not Assigned(grp) then Continue;
    for j := 0 to Pred(ElementCount(grp)) do begin
      r := ElementByIndex(grp, j);
      if SameText(EditorID(r), edid) then begin
        Result := WinningOverride(r);
        Exit;
      end;
    end;
  end;
end;

function Hex8(r: IwbMainRecord): string;
begin
  Result := IntToHex(GetLoadOrderFormID(r), 8);
end;

function Hex6(r: IwbMainRecord): string;
begin
  Result := IntToHex(GetLoadOrderFormID(r) and $00FFFFFF, 6);
end;

// Recursive element dump.
procedure Tree(el: IInterface; indent: string; depth: Integer);
var
  i: Integer;
  child: IInterface;
  v: string;
begin
  if not Assigned(el) then Exit;
  if depth <= 0 then Exit;
  for i := 0 to Pred(ElementCount(el)) do begin
    child := ElementByIndex(el, i);
    try
      v := GetEditValue(child);
    except
      v := '';
    end;
    if v <> '' then
      Say(indent + Name(child) + ' = ' + v)
    else
      Say(indent + Name(child));
    if ElementCount(child) > 0 then
      Tree(child, indent + '  ', depth - 1);
  end;
end;

function KwdaStr(r: IwbMainRecord): string;
var
  kwda: IInterface;
  i: Integer;
begin
  Result := '';
  kwda := ElementBySignature(r, 'KWDA');
  if Assigned(kwda) then
    for i := 0 to Pred(ElementCount(kwda)) do
      Result := Result + ' ' + GetEditValue(ElementByIndex(kwda, i));
  if Result = '' then Result := ' <none>';
end;

procedure DumpDiseaseSpell(edid: string);
var
  spl, mg: IwbMainRecord;
  effects, eff, efid, conds: IInterface;
  i: Integer;
begin
  Say('');
  Say('--- SPEL ' + edid + ' ---');
  spl := FindWinning('SPEL', edid);
  if not Assigned(spl) then begin
    Say('  NOT FOUND');
    Exit;
  end;
  Say('  ' + Hex8(spl) + '  winning file: ' + GetFileName(GetFile(spl)));
  Say('  SPIT Type/Cast/Target = '
    + GetElementEditValues(spl, 'SPIT\Type') + ' / '
    + GetElementEditValues(spl, 'SPIT\Cast Type') + ' / '
    + GetElementEditValues(spl, 'SPIT\Target Type'));

  effects := ElementByName(spl, 'Effects');
  if not Assigned(effects) then Exit;
  for i := 0 to Pred(ElementCount(effects)) do begin
    eff := ElementByIndex(effects, i);
    efid := ElementByPath(eff, 'EFID');
    mg := LinksTo(efid);
    Say('  effect[' + IntToStr(i) + '] EFID = ' + GetEditValue(efid)
      + '   EFIT mag/area/dur = '
      + GetElementEditValues(eff, 'EFIT\Magnitude') + ' / '
      + GetElementEditValues(eff, 'EFIT\Area') + ' / '
      + GetElementEditValues(eff, 'EFIT\Duration'));
    conds := ElementByName(eff, 'Conditions');
    if Assigned(conds) and (ElementCount(conds) > 0) then begin
      Say('    effect conditions:');
      Tree(conds, '      ', 4);
    end;
    if Assigned(mg) then begin
      Say('    MGEF ' + EditorID(mg) + ' ' + Hex8(mg)
        + '  arch=' + GetElementEditValues(mg, 'Magic Effect Data\DATA\Archtype')
        + '  AV=' + GetElementEditValues(mg, 'Magic Effect Data\DATA\Actor Value'));
      Say('    MGEF KWDA:' + KwdaStr(mg));
      conds := ElementByName(mg, 'Conditions');
      if Assigned(conds) and (ElementCount(conds) > 0) then begin
        Say('    MGEF conditions:');
        Tree(conds, '      ', 4);
      end;
    end;
  end;
end;

// Scan every loaded file's group for EditorIDs matching any substring.
procedure ScanEDID(sig: string; subs: TStringList; withTree: Boolean);
var
  i, j, k: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
  eid: string;
  hit: Boolean;
begin
  Say('');
  Say('=== ' + sig + ' by EditorID mask ===');
  for i := 0 to Pred(FileCount) do begin
    grp := GroupBySignature(FileByIndex(i), sig);
    if not Assigned(grp) then Continue;
    for j := 0 to Pred(ElementCount(grp)) do begin
      r := ElementByIndex(grp, j);
      eid := EditorID(r);
      hit := False;
      for k := 0 to Pred(subs.Count) do
        if Has(eid, subs[k]) then hit := True;
      if hit then begin
        Say('  [' + Hex6(r) + '] ' + eid + '  "'
          + GetElementEditValues(r, 'FULL') + '"  (' + GetFileName(FileByIndex(i)) + ')');
        if withTree then begin
          Tree(WinningOverride(r), '      ', 3);
          Say('');
        end;
      end;
    end;
  end;
end;

procedure DumpRace(edid: string);
var
  r: IwbMainRecord;
begin
  r := FindWinning('RACE', edid);
  if not Assigned(r) then begin
    Say('  RACE ' + edid + ' NOT FOUND');
    Exit;
  end;
  Say('  RACE ' + edid + ' [' + Hex6(r) + '] (' + GetFileName(GetFile(r)) + ')  KWDA:' + KwdaStr(r));
end;

procedure DumpFoodRaw;
var
  i, j, k: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
  kwda: IInterface;
  isRaw: Boolean;
begin
  Say('');
  Say('=== ALCH: VendorItemFoodRaw keyword OR "raw" in EditorID ===');
  for i := 0 to Pred(FileCount) do begin
    grp := GroupBySignature(FileByIndex(i), 'ALCH');
    if not Assigned(grp) then Continue;
    for j := 0 to Pred(ElementCount(grp)) do begin
      r := WinningOverride(ElementByIndex(grp, j));
      isRaw := Has(EditorID(r), 'raw');
      kwda := ElementBySignature(r, 'KWDA');
      if Assigned(kwda) then
        for k := 0 to Pred(ElementCount(kwda)) do
          if SameText(GetEditValue(ElementByIndex(kwda, k)), 'VendorItemFoodRaw') then
            isRaw := True;
      if isRaw then
        Say('  [' + Hex6(r) + '] ' + EditorID(r) + '  "'
          + GetElementEditValues(r, 'FULL') + '" (' + GetFileName(GetFile(r)) + ')');
    end;
  end;
end;

function Initialize: Integer;
var
  subs: TStringList;
begin
  Result := 0;
  Say('============================================================');
  Say(' RFAB disease audit (Prep 1)');
  Say('============================================================');

  DumpDiseaseSpell('TrapDiseaseAtaxia');
  DumpDiseaseSpell('TrapDiseaseRockjoint');
  DumpDiseaseSpell('TrapDiseaseBrainRot');
  DumpDiseaseSpell('TrapDiseaseRattles');
  DumpDiseaseSpell('TrapDiseaseBoneBreakFever');
  DumpDiseaseSpell('TrapDiseaseWitbane');
  DumpDiseaseSpell('DLC2DiseaseDroops');
  DumpDiseaseSpell('DiseaseDroops');
  DumpDiseaseSpell('dunHaknirDeathDroops');

  subs := TStringList.Create;
  try
    subs.Add('peryite');
    subs.Add('blessing');
    subs.Add('afflict');
    subs.Add('inocul');
    subs.Add('tincture');
    subs.Add('onlycure');
    ScanEDID('SPEL', subs, False);
    ScanEDID('PERK', subs, False);
    ScanEDID('MGEF', subs, True);
  finally
    subs.Free;
  end;

  Say('');
  Say('=== candidate races ===');
  DumpRace('SlaughterfishRace');
  DumpRace('DraugrRace');
  DumpRace('TrollRace');
  DumpRace('TrollFrostRace');
  DumpRace('DLC1TrollRaceArmored');
  DumpRace('DLC2TrollRimeRace');
  DumpRace('OrcRace');
  DumpRace('OrcRaceVampire');
  DumpRace('KhajiitRace');
  DumpRace('KhajiitRaceVampire');
  DumpRace('BosmerRace');
  DumpRace('BosmerRaceVampire');
  DumpRace('ArgonianRace');
  DumpRace('ArgonianRaceVampire');

  subs := TStringList.Create;
  try
    subs.Add('stomach');
    subs.Add('rawfood');
    subs.Add('rawmeat');
    subs.Add('foodpoison');
    subs.Add('digest');
    subs.Add('ironstom');
    ScanEDID('KYWD', subs, False);
    ScanEDID('FLST', subs, False);
    ScanEDID('GLOB', subs, False);
  finally
    subs.Free;
  end;

  DumpFoodRaw;

  subs := TStringList.Create;
  try
    subs.Add('healrate');
    subs.Add('healthregen');
    subs.Add('noregen');
    subs.Add('regendisable');
    subs.Add('stopregen');
    subs.Add('regenmult');
    ScanEDID('MGEF', subs, True);
    ScanEDID('SPEL', subs, False);
  finally
    subs.Free;
  end;

  Say('');
  Say('=== audit done ===');
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
