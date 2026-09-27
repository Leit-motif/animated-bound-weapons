# Write THIRD-PARTY-NOTICES.txt from the license files of what the DLL links: the pinned
# CommonLibSSE-NG checkout (FetchContent, so run .\build.ps1 first) and the vcpkg packages.
# Regenerate whenever the CommonLib pin or the vcpkg package list changes.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Skse = Join-Path $Root "tools\AnimatedBoundWeaponsSKSE"
$cmake = Get-Content (Join-Path $Skse "CMakeLists.txt") -Raw
if ($cmake -notmatch 'set\(ABW_COMMONLIBSSE_GIT_TAG "([^"]+)"\)') { throw "No ABW_COMMONLIBSSE_GIT_TAG in CMakeLists.txt" }
$tag = $Matches[1]
if ($cmake -notmatch 'GIT_REPOSITORY (\S+)') { throw "No CommonLib GIT_REPOSITORY in CMakeLists.txt" }
$repo = $Matches[1] -replace '\.git$', ''

$commonlib = Join-Path $Skse "build\release\_deps\commonlibsse-src"
foreach ($f in @("EXCEPTIONS.md", "licenses\LICENSE-MIT.txt", "licenses\LICENSE-hde64.txt")) {
    if (-not (Test-Path (Join-Path $commonlib $f))) { throw "Missing $f in $commonlib — run .\build.ps1 first" }
}
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }
$share = Join-Path $vcpkgRoot "installed\x64-windows-static\share"

function Read-Text([string]$Path) { (Get-Content -LiteralPath $Path -Raw).Trim() }

$sections = [System.Collections.Generic.List[string]]::new()
$sections.Add(@"
CommonLibSSE-NG ($repo, commit $tag)

GPL-3.0-or-later with the exceptions below. The GPL text is in LICENSE.

$(Read-Text (Join-Path $commonlib "EXCEPTIONS.md"))

Originally based on code under the MIT License:

$(Read-Text (Join-Path $commonlib "licenses\LICENSE-MIT.txt"))
"@)
$sections.Add(@"
Hacker Disassembler Engine 64 (vendored in CommonLibSSE-NG)

$(Read-Text (Join-Path $commonlib "licenses\LICENSE-hde64.txt"))
"@)
$sections.Add(@"
SKSE Menu Framework SDK

tools/AnimatedBoundWeaponsSKSE/extern/SKSEMenuFramework.h, the public header of SKSE Menu Framework
by Thiago (https://www.nexusmods.com/skyrimspecialedition/mods/120352). The header ships without a
license statement.
"@)
foreach ($pkg in @("fmt", "spdlog", "xbyak", "rapidcsv", "directxtk")) {
    $copyright = Join-Path $share "$pkg\copyright"
    if (-not (Test-Path $copyright)) { throw "Missing $copyright — run .\build.ps1 first" }
    $sections.Add("$pkg`n`n$(Read-Text $copyright)")
}

$text = "Third-party notices`n`n`n" + ($sections -join "`n`n`n") + "`n"
$out = Join-Path $Root "THIRD-PARTY-NOTICES.txt"
[System.IO.File]::WriteAllText($out, ($text -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote $out ($($sections.Count) sections, CommonLibSSE-NG $tag)"
