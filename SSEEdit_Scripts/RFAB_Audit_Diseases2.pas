{
  Prep-1 audit, round 2. Read-only, not shipped.
  Run: right-click ANY plugin -> Apply Script -> RFAB_Audit_Diseases2

  Answers the questions round 1 left open:
   A. Does RFAB have its OWN Ataxia/Rockjoint/Witbane/Rattles/BoneBreakFever/
      BrainRot (or a generic Requiem disease framework)?  Broad EDID scan of
      SPEL + MGEF, with SPIT type, VMAD script names, effect list, and FULLY
      EXPANDED conditions (CTDA function + params + operator).
   B. TrapDiseaseAtaxia + RFAB_Effect_Null: full record, DATA flags, VMAD.
   C. How the Peryite per-disease effects are gated: dump RFAB_Blessing_Peryite
      / RFAB_Perk_PeryiteTouch / every RFAB_Effect_Peryite* with real CTDA.
   D. Race gaps: WoodElfRace(+vampire), draugr variants, strong-stomach keyword
      holders.
}
unit RFAB_Audit_Diseases2;

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

function Hex6(r: IwbMainRecord): string;
begin
  Result := IntToHex(GetLoadOrderFormID(r) and $00FFFFFF, 6);
end;

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

function VmadNames(r: IwbMainRecord): string;
var
  vmad, scripts, scr: IInterface;
  i: Integer;
begin
  Result := '';
  vmad := ElementByName(r, 'VMAD - Virtual Machine Adapter');
  if not Assigned(vmad) then vmad := ElementBySignature(r, 'VMAD');
  if not Assigned(vmad) then Exit;
  scripts := ElementByName(vmad, 'Scripts');
  if not Assigned(scripts) then Exit;
  for i := 0 to Pred(ElementCount(scripts)) do begin
    scr := ElementByIndex(scripts, i);
    Result := Result + ' ' + GetElementEditValues(scr, 'scriptName');
  end;
end;

function CtdaLine(cond: IInterface): string;
var
  ctda: IInterface;
  fn, op, cmp, p1, p2, ro, rf: string;
begin
  ctda := ElementBySignature(cond, 'CTDA');
  if not Assigned(ctda) then ctda := cond;
  fn  := GetElementEditValues(ctda, 'Function');
  op  := GetElementEditValues(ctda, 'Type');
  cmp := GetElementEditValues(ctda, 'Comparison Value');
  if cmp = '' then cmp := GetElementEditValues(ctda, 'Comparison Value - Float');
  p1  := GetElementEditValues(ctda, 'Parameter #1');
  p2  := GetElementEditValues(ctda, 'Parameter #2');
  ro  := GetElementEditValues(ctda, 'Run On');
  rf  := GetElementEditValues(ctda, 'Reference');
  Result := fn + ' (' + p1;
  if p2 <> '' then Result := Result + ', ' + p2;
  Result := Result + ') ' + op + ' ' + cmp;
  if (ro <> '') and (ro <> 'Subject') then
    Result := Result + '  [RunOn ' + ro + ' ' + rf + ']';
end;

procedure DumpConds(owner: IInterface; indent: string);
var
  conds, c: IInterface;
  i: Integer;
begin
  conds := ElementByName(owner, 'Conditions');
  if not Assigned(conds) then Exit;
  for i := 0 to Pred(ElementCount(conds)) do begin
    c := ElementByIndex(conds, i);
    Say(indent + '- ' + CtdaLine(c));
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

procedure DumpSpel(r: IwbMainRecord);
var
  effects, eff, efid: IInterface;
  mg: IwbMainRecord;
  i: Integer;
begin
  Say('  SPEL [' + Hex6(r) + '] ' + EditorID(r) + '  "'
    + GetElementEditValues(r, 'FULL') + '"  (' + GetFileName(GetFile(r)) + ')');
  Say('    SPIT ' + GetElementEditValues(r, 'SPIT\Type') + ' / '
    + GetElementEditValues(r, 'SPIT\Cast Type') + ' / '
    + GetElementEditValues(r, 'SPIT\Target Type')
    + '   script:' + VmadNames(r));
  effects := ElementByName(r, 'Effects');
  if not Assigned(effects) then Exit;
  for i := 0 to Pred(ElementCount(effects)) do begin
    eff := ElementByIndex(effects, i);
    efid := ElementByPath(eff, 'EFID');
    mg := LinksTo(efid);
    Say('    eff[' + IntToStr(i) + '] ' + GetEditValue(efid)
      + '  mag/area/dur ' + GetElementEditValues(eff, 'EFIT\Magnitude')
      + '/' + GetElementEditValues(eff, 'EFIT\Area')
      + '/' + GetElementEditValues(eff, 'EFIT\Duration'));
    DumpConds(eff, '        cond ');
    if Assigned(mg) then begin
      Say('      MGEF ' + EditorID(mg) + ' [' + Hex6(mg) + ']  arch='
        + GetElementEditValues(mg, 'Magic Effect Data\DATA\Archtype')
        + '  AV=' + GetElementEditValues(mg, 'Magic Effect Data\DATA\Actor Value')
        + '  script:' + VmadNames(mg));
      Say('      MGEF KWDA:' + KwdaStr(mg));
      DumpConds(mg, '        mgef-cond ');
    end;
  end;
end;

procedure DumpMgef(r: IwbMainRecord);
begin
  Say('  MGEF [' + Hex6(r) + '] ' + EditorID(r) + '  "'
    + GetElementEditValues(r, 'FULL') + '"  (' + GetFileName(GetFile(r)) + ')');
  Say('    arch=' + GetElementEditValues(r, 'Magic Effect Data\DATA\Archtype')
    + '  AV=' + GetElementEditValues(r, 'Magic Effect Data\DATA\Actor Value')
    + '  cast=' + GetElementEditValues(r, 'Magic Effect Data\DATA\Casting Type')
    + '  deliv=' + GetElementEditValues(r, 'Magic Effect Data\DATA\Delivery')
    + '  assoc=' + GetElementEditValues(r, 'Magic Effect Data\DATA\Assoc. Item')
    + '  script:' + VmadNames(r));
  Say('    KWDA:' + KwdaStr(r) + '   DNAM=' + GetElementEditValues(r, 'DNAM'));
  DumpConds(r, '      cond ');
end;

procedure ScanBySig(sig: string; subs: TStringList);
var
  i, j, k: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
  eid: string;
  hit: Boolean;
begin
  Say('');
  Say('=== ' + sig + ' scan ===');
  for i := 0 to Pred(FileCount) do begin
    grp := GroupBySignature(FileByIndex(i), sig);
    if not Assigned(grp) then Continue;
    for j := 0 to Pred(ElementCount(grp)) do begin
      r := ElementByIndex(grp, j);
      if not IsWinningOverride(r) then Continue;
      eid := EditorID(r);
      hit := False;
      for k := 0 to Pred(subs.Count) do
        if Has(eid, subs[k]) then hit := True;
      if not hit then Continue;
      if sig = 'SPEL' then DumpSpel(r)
      else if sig = 'MGEF' then DumpMgef(r)
      else Say('  [' + Hex6(r) + '] ' + eid + '  "'
        + GetElementEditValues(r, 'FULL') + '"  (' + GetFileName(GetFile(r)) + ')');
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
  Say('  RACE ' + edid + ' [' + Hex6(r) + '] KWDA:' + KwdaStr(r));
end;

procedure RacesWithKwd(kwdEdid: string);
var
  i, j, k: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
  kwda: IInterface;
begin
  Say('');
  Say('=== RACE holders of keyword ' + kwdEdid + ' ===');
  for i := 0 to Pred(FileCount) do begin
    grp := GroupBySignature(FileByIndex(i), 'RACE');
    if not Assigned(grp) then Continue;
    for j := 0 to Pred(ElementCount(grp)) do begin
      r := ElementByIndex(grp, j);
      if not IsWinningOverride(r) then Continue;
      kwda := ElementBySignature(r, 'KWDA');
      if not Assigned(kwda) then Continue;
      for k := 0 to Pred(ElementCount(kwda)) do
        if SameText(GetEditValue(ElementByIndex(kwda, k)), kwdEdid) then begin
          Say('  ' + EditorID(r) + ' [' + Hex6(r) + ']');
          Break;
        end;
    end;
  end;
end;

function Initialize: Integer;
var
  subs: TStringList;
  r: IwbMainRecord;
begin
  Result := 0;
  Say('============================================================');
  Say(' RFAB disease audit - round 2');
  Say('============================================================');

  subs := TStringList.Create;
  try
    subs.Add('ataxia');    subs.Add('rockjoint');  subs.Add('witbane');
    subs.Add('rattles');   subs.Add('bonebreak');  subs.Add('brainrot');
    subs.Add('disease');   subs.Add('blight');     subs.Add('sickness');
    subs.Add('plague');    subs.Add('infection');  subs.Add('incubat');
    subs.Add('contagion'); subs.Add('req_dis');    subs.Add('droops');
    ScanBySig('SPEL', subs);
    ScanBySig('MGEF', subs);
  finally
    subs.Free;
  end;

  Say('');
  Say('=== TrapDiseaseAtaxia (full) ===');
  r := FindWinning('SPEL', 'TrapDiseaseAtaxia');
  if Assigned(r) then begin
    DumpSpel(r);
    Say('  spell flags = ' + GetElementEditValues(r, 'SPIT\Spell Flags'));
  end;
  Say('');
  Say('=== RFAB_Effect_Null (full) ===');
  r := FindWinning('MGEF', 'RFAB_Effect_Null');
  if Assigned(r) then DumpMgef(r);

  Say('');
  Say('=== Peryite blessing chain ===');
  r := FindWinning('SPEL', 'RFAB_Blessing_Peryite');
  if Assigned(r) then DumpSpel(r);
  r := FindWinning('SPEL', 'RFAB_Spell_PeryiteTouch_Damage');
  if Assigned(r) then DumpSpel(r);
  r := FindWinning('SPEL', 'REQ_Blessing_ForCure');
  if Assigned(r) then DumpSpel(r);
  r := FindWinning('PERK', 'RFAB_Perk_PeryiteTouch');
  if Assigned(r) then begin
    Say('  PERK RFAB_Perk_PeryiteTouch [' + Hex6(r) + ']');
    DumpConds(r, '    perk-cond ');
  end;

  subs := TStringList.Create;
  try
    subs.Add('peryite');
    ScanBySig('MGEF', subs);
  finally
    subs.Free;
  end;

  Say('');
  Say('=== race gaps ===');
  DumpRace('WoodElfRace');
  DumpRace('WoodElfRaceVampire');
  DumpRace('BosmerRace');
  DumpRace('DraugrRace');
  DumpRace('DraugrSkeletonRace');
  DumpRace('DLC1DraugrRace');
  DumpRace('SprigganRace');
  RacesWithKwd('REQ_KW_StrongStomach');
  RacesWithKwd('ActorTypeUndead');

  Say('');
  Say('=== audit2 done ===');
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
