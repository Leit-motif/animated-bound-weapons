# Build Animated Bound Weapons: ESP + SKSE plugin DLL.
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Mod = Join-Path $Root "AnimatedBoundWeapons"
$SkseProject = Join-Path $Root "tools\AnimatedBoundWeaponsSKSE"
$Cmake = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path $Cmake)) {
    throw "CMake not found at $Cmake"
}

function Get-VcpkgRoot {
    $candidates = @()
    if ($env:VCPKG_ROOT) {
        $candidates += $env:VCPKG_ROOT
    }
    $candidates += "C:\vcpkg"
    foreach ($root in $candidates) {
        if (-not $root) { continue }
        $toolchain = Join-Path $root "scripts\buildsystems\vcpkg.cmake"
        $exe = Join-Path $root "vcpkg.exe"
        if ((Test-Path $toolchain) -and (Test-Path $exe)) {
            return $root
        }
    }
    throw "vcpkg not found. Set VCPKG_ROOT to a vcpkg checkout that contains vcpkg.exe, or install vcpkg at C:\vcpkg."
}

function Ensure-VcpkgStaticPackages([string]$VcpkgRoot) {
    $triplet = "x64-windows-static"
    $needed = @("fmt", "spdlog", "xbyak", "rapidcsv")
    $missing = @()
    foreach ($pkg in $needed) {
        $share = Join-Path $VcpkgRoot "installed\$triplet\share\$pkg"
        if (-not (Test-Path $share)) {
            $missing += "${pkg}:${triplet}"
        }
    }
    if ($missing.Count -eq 0) {
        return
    }
    $exe = Join-Path $VcpkgRoot "vcpkg.exe"
    Write-Host "Installing vcpkg packages: $($missing -join ', ')"
    & $exe install @missing
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed (exit $LASTEXITCODE)"
    }
}

$VcpkgRoot = Get-VcpkgRoot
Ensure-VcpkgStaticPackages $VcpkgRoot
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

Write-Host "Building ESP generator..."
dotnet build (Join-Path $Root "tools\GenerateEsp\GenerateEsp.csproj") -c Release | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "GenerateEsp build failed (exit $LASTEXITCODE)"
}
$gen = Join-Path $Root "tools\GenerateEsp\bin\Release\net8.0\GenerateEsp.exe"
& $gen
if ($LASTEXITCODE -ne 0) {
    throw "GenerateEsp failed (exit $LASTEXITCODE)"
}

Write-Host "Verifying ESP..."
dotnet test (Join-Path $Root "tools\VerifyEsp.Tests\VerifyEsp.Tests.csproj") -c Release --nologo | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "VerifyEsp tests failed (exit $LASTEXITCODE)"
}
dotnet run --project (Join-Path $Root "tools\VerifyEsp\VerifyEsp.csproj") -c Release | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "VerifyEsp failed (exit $LASTEXITCODE)"
}

Write-Host "Building SKSE plugin..."
Push-Location $SkseProject
try {
    $buildDir = "build/release"
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
        $cacheText = Get-Content $cacheFile -Raw
        if ($cacheText -match 'tools[/\\]deps|spellscribe-stances') {
            Write-Host "Discarding SKSE CMake cache (toolchain moved off SSS tools/deps). Reconfigure required once."
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }
    }
    $cmakeArgs = @("-S", ".", "-B", $buildDir, "-DCMAKE_BUILD_TYPE=Release")
    if (-not (Test-Path $cacheFile)) {
        $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain"
        $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows-static"
        & $Cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }
    }
    & $Cmake --build $buildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw "SKSE plugin build failed (exit $LASTEXITCODE)" }
}
finally {
    Pop-Location
}

Write-Host "Build complete."
Write-Host "Package: $Mod"
Get-ChildItem $Mod -Recurse -Include *.esp,*.dll,*.ini | ForEach-Object { $_.FullName.Substring($Root.Length + 1) }
