{
  Throwaway: what keywords tell food apart from drink / alcohol / water.
  Run: right-click ANY plugin -> Apply Script -> RFAB_Audit_Drinks
  Dumps every winning ALCH whose EDID/FULL looks edible OR that carries a
  food/drink/alcohol keyword: EDID, FULL, ENIT flags (raw + text), KWDA.
}
unit RFAB_Audit_Drinks;

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

function HitAny(s: string): Boolean;
begin
  Result := Has(s, 'food') or Has(s, 'drink') or Has(s, 'alcohol')
    or Has(s, 'wine') or Has(s, 'water') or Has(s, 'ale') or Has(s, 'mead')
    or Has(s, 'beer') or Has(s, 'juice') or Has(s, 'milk') or Has(s, 'bread')
    or Has(s, 'meat') or Has(s, 'roast') or Has(s, 'soup') or Has(s, 'stew')
    or Has(s, 'cheese') or Has(s, 'apple') or Has(s, 'broth') or Has(s, 'rum')
    or Has(s, 'beverage');
end;

function Interesting(edid, full, kwda: string): Boolean;
begin
  Result := HitAny(edid) or HitAny(full) or HitAny(kwda);
end;

function Initialize: Integer;
var
  i, j: Integer;
  grp: IwbGroupRecord;
  r: IwbMainRecord;
  edid, full, kwda, enit, flags: string;
begin
  Result := 0;
  Say('=== ALCH: edible-looking / food-drink keyword (winning) ===');
  for i := 0 to Pred(FileCount) do begin
    grp := GroupBySignature(FileByIndex(i), 'ALCH');
    if not Assigned(grp) then Continue;
    for j := 0 to Pred(ElementCount(grp)) do begin
      r := ElementByIndex(grp, j);
      if not IsWinningOverride(r) then Continue;
      edid := EditorID(r);
      full := GetElementEditValues(r, 'FULL - Name');
      kwda := KwdaStr(r);
      if not Interesting(edid, full, kwda) then Continue;

      flags := GetElementEditValues(r, 'ENIT - Effect Data\Flags');
      enit  := GetElementEditValues(r, 'ENIT - Effect Data\Flags\Food Item');
      Say('[' + Hex6(r) + '] ' + edid + '  "' + full + '"  ('
        + GetFileName(GetFile(r)) + ')');
      Say('    ENIT flags="' + flags + '"  FoodBit="' + enit + '"');
      Say('    KWDA:' + kwda);
    end;
  end;
  Say('=== done ===');
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
