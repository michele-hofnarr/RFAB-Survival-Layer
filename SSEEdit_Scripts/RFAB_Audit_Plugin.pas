{
  Audit of RFAB_SurvivalLayer.esp - dumps the actual values of every record the
  generator created (GLOB type/value, QUST Start-Game-Enabled flag + bound
  scripts, MGEF archetype/AV/flags, SPEL type/cast/effect count, FLST size).
  Diagnostic only, not shipped. Run: right-click RFAB_SurvivalLayer.esp ->
  Apply Script -> RFAB_Audit_Plugin.
}
unit RFAB_Audit_Plugin;

const
  PLUGIN_NAME = 'RFAB_SurvivalLayer.esp';

var
  tgt: IwbFile;

procedure Say(s: string);
begin
  AddMessage(s);
end;

function Val(rec: IInterface; path: string): string;
var
  el: IInterface;
begin
  el := ElementByPath(rec, path);
  if Assigned(el) then
    Result := GetEditValue(el)
  else
    Result := '<no path ' + path + '>';
end;

// Names of the scripts bound via VMAD.
function ScriptsOf(rec: IwbMainRecord): string;
var
  vmad, scripts, scr: IInterface;
  i: Integer;
begin
  Result := '';

  vmad := ElementByPath(rec, 'VMAD');
  if not Assigned(vmad) then begin
    Result := '<no VMAD>';
    Exit;
  end;

  scripts := ElementByPath(vmad, 'Scripts');
  if not Assigned(scripts) then begin
    Result := '<no Scripts>';
    Exit;
  end;

  for i := 0 to Pred(ElementCount(scripts)) do begin
    scr := ElementByIndex(scripts, i);
    if Result <> '' then Result := Result + ', ';
    Result := Result + GetElementEditValues(scr, 'scriptName');
  end;

  if Result = '' then
    Result := '<empty list>';
end;

procedure DumpGroup(sig: string);
var
  grp: IwbGroupRecord;
  r  : IwbMainRecord;
  i  : Integer;
  n  : Integer;
begin
  grp := GroupBySignature(tgt, sig);
  Say('');
  if not Assigned(grp) then begin
    Say('===== ' + sig + ': NO GROUP =====');
    Exit;
  end;

  n := ElementCount(grp);
  Say('===== ' + sig + ': ' + IntToStr(n) + ' records =====');

  for i := 0 to Pred(n) do begin
    r := ElementByIndex(grp, i);

    if sig = 'GLOB' then
      Say('  ' + EditorID(r) + '  type=' + Val(r, 'FNAM') + '  value=' + Val(r, 'FLTV'))

    else if sig = 'QUST' then begin
      Say('  ' + EditorID(r));
      Say('      flags   = ' + Val(r, 'DNAM\Flags'));
      Say('      scripts = ' + ScriptsOf(r));
    end

    else if sig = 'MGEF' then begin
      Say('  ' + EditorID(r));
      Say('      archetype = ' + Val(r, 'Magic Effect Data\DATA\Archtype')
                + '  / ' + Val(r, 'Magic Effect Data\DATA\Casting Type')
                + '  / ' + Val(r, 'Magic Effect Data\DATA\Delivery'));
      Say('      AV      = ' + Val(r, 'Magic Effect Data\DATA\Actor Value'));
      Say('      flags   = ' + Val(r, 'Magic Effect Data\DATA\Flags'));
      Say('      scripts = ' + ScriptsOf(r));
    end

    else if sig = 'SPEL' then begin
      Say('  ' + EditorID(r));
      Say('      type    = ' + Val(r, 'SPIT\Type')
                + '  / ' + Val(r, 'SPIT\Cast Type')
                + '  / ' + Val(r, 'SPIT\Target Type'));
      Say('      effects = ' + IntToStr(ElementCount(ElementByName(r, 'Effects'))));
    end

    else if sig = 'FLST' then
      Say('  ' + EditorID(r) + '  items='
            + IntToStr(ElementCount(ElementByName(r, 'FormIDs'))));
  end;
end;

function Initialize: Integer;
var
  i: Integer;
begin
  Result := 0;

  tgt := nil;
  for i := 0 to Pred(FileCount) do
    if SameText(GetFileName(FileByIndex(i)), PLUGIN_NAME) then
      tgt := FileByIndex(i);

  if not Assigned(tgt) then begin
    Say(PLUGIN_NAME + ' not loaded');
    Result := 1;
    Exit;
  end;

  Say('============================================================');
  Say(' Audit ' + PLUGIN_NAME);
  Say('============================================================');

  DumpGroup('GLOB');
  DumpGroup('FLST');
  DumpGroup('MGEF');
  DumpGroup('SPEL');
  DumpGroup('QUST');

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
