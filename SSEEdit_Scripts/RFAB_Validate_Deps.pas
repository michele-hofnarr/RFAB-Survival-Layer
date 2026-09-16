{
  RFAB Survival Layer - dependency validator.

  Apply Script against the active load order (any file selected). Prints a
  PASS/FAIL report for every external record the mod resolves - the formIDs
  native/src/Core/Forms.cpp hardcodes, and the EditorIDs the generator and
  HasKeywordString() look up.

  The table is NOT here: it is RFAB_deps.txt, loaded below, and tools/check_deps.py
  reads the same file. One table, two readers - a table kept in two places is a
  table that disagrees with itself after the second RFAB update.

  For the quick answer after an RFAB update, run tools/check_deps.py: it parses
  the plugins directly, takes a second and needs no xEdit. This script is for
  what only xEdit can say - the WINNING override of each record, and whether the
  archetype templates the generator copies from still exist.

  FAIL on a FormID line = that local id no longer holds the expected kind of
  record in that file (RFAB reindexed / removed it). A rename alone still
  PASSES but prints the new EditorID - eyeball the list.
}
unit RFAB_Validate_Deps;

const
  SELF_PLUGIN = 'RFAB_SurvivalLayer.esp';
  DEP_TABLE   = 'RFAB_deps.txt';

var
  passN, failN, warnN: Integer;

procedure Line(s: string);
begin
  AddMessage(s);
end;

// The dependency table, read from RFAB_deps.txt next to this script.
//   file | key | SIG | note
//   key '#<hex6>' -> match by local FormID (what Forms.cpp hardcodes)
//   key '<edid>'  -> match by EditorID (generator / HasKeywordString)
procedure DepTable(sl: TStringList);
var
  raw : TStringList;
  i   : Integer;
  ln  : string;
begin
  raw := TStringList.Create;
  try
    raw.LoadFromFile(ScriptsPath + DEP_TABLE);
    for i := 0 to Pred(raw.Count) do begin
      ln := Trim(raw[i]);
      if (ln = '') or (Copy(ln, 1, 1) = '#') then Continue;
      sl.Add(ln);
    end;
  finally
    raw.Free;
  end;
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
  Line('  GLOB should be 0 since v0.5.0 - settings live in the ini, not in globals.');
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
