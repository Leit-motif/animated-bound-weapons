# Write AnimatedBoundWeapons/Interface/Translations/AnimatedBoundWeapons_ENGLISH.txt from
# tools/AnimatedBoundWeaponsSKSE/include/Strings.inc, the single source of the DLL's
# user-facing strings. The engine reads this format (UTF-16LE with BOM, "$KEY<tab>text",
# CRLF) through SKSE::Translation; xTranslator edits it. A translator copies the file
# to AnimatedBoundWeapons_<LANGUAGE>.txt and replaces the right-hand side.
#
# Run by build.ps1; run alone to regenerate after editing Strings.inc.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $Root "tools\AnimatedBoundWeaponsSKSE\include\Strings.inc"
$OutDir = Join-Path $Root "AnimatedBoundWeapons\Interface\Translations"
$Out = Join-Path $OutDir "AnimatedBoundWeapons_ENGLISH.txt"

$pattern = '^\s*ABW_STR\(\s*(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)\s*$'
$lines = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($raw in [System.IO.File]::ReadAllLines($Source, [System.Text.Encoding]::UTF8)) {
    if ($raw -notmatch $pattern) {
        if ($raw -match '^\s*ABW_STR') { throw "Unparsable ABW_STR line: $raw" }
        continue
    }
    $id = $Matches[1]
    $rawText = $Matches[2]
    # Check escapes on the raw text, before unescaping, so a legitimate "\\n" passes.
    if ($rawText -match '(^|[^\\])(\\\\)*\\[^"\\]') { throw "Unsupported escape in $id" }
    $text = $rawText -replace '\\"', '"' -replace '\\\\', '\'
    if ($seen.ContainsKey($id)) { throw "Duplicate string id $id" }
    $seen[$id] = $true
    $lines.Add("`$ABW_$id`t$text")
}
if ($lines.Count -eq 0) { throw "No ABW_STR entries found in $Source" }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$body = ($lines -join "`r`n") + "`r`n"
# UTF-16LE with BOM: Encoding.Unicode writes the FF FE preamble via GetPreamble.
$enc = New-Object System.Text.UnicodeEncoding($false, $true)
$bytes = $enc.GetPreamble() + $enc.GetBytes($body)
[System.IO.File]::WriteAllBytes($Out, $bytes)

# The check ParseTranslation performs silently: FF FE or the file is ignored.
$head = [System.IO.File]::ReadAllBytes($Out)[0..1]
if ($head[0] -ne 0xFF -or $head[1] -ne 0xFE) { throw "$Out does not start with a UTF-16LE BOM" }
Write-Host "Wrote $Out ($($lines.Count) keys)"
