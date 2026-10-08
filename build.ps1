# ============================================================
#  Organic Chemistry — build script (Ninja + MSVC)
#
#  Why this does not use Enter-VsDevShell:
#    The VS DevShell module builds a case-insensitive dictionary of the
#    environment. When both `http_proxy` and `HTTP_PROXY` exist (which some
#    tooling sets), it throws "Item has already been added" and leaves PATH
#    half-written, so even `findstr` disappears. Setting the toolchain
#    variables directly is deterministic and has no such failure mode.
# ============================================================

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir   = Join-Path $projectDir "cmake-build-ninja"
$juceSrc    = Join-Path $projectDir "cmake-build-release-visual-studio\_deps\juce-src"
$cmake      = "C:\Program Files\CMake\bin\cmake.exe"

$vsPath  = "C:\Program Files\Microsoft Visual Studio\18\Professional"
$sdkRoot = "C:\Program Files (x86)\Windows Kits\10"

# --- Pick the newest installed MSVC toolset and Windows SDK ---------------

$msvcVer = Get-ChildItem "$vsPath\VC\Tools\MSVC" -Directory |
           Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name

$sdkVer = Get-ChildItem "$sdkRoot\Include" -Directory |
          Where-Object { Test-Path "$sdkRoot\Lib\$($_.Name)\um\x64" } |
          Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name

$msvcDir = "$vsPath\VC\Tools\MSVC\$msvcVer"

Write-Output "MSVC $msvcVer / Windows SDK $sdkVer"

# --- Compose the toolchain environment ------------------------------------

$env:INCLUDE = @(
    "$msvcDir\include",
    "$sdkRoot\Include\$sdkVer\ucrt",
    "$sdkRoot\Include\$sdkVer\shared",
    "$sdkRoot\Include\$sdkVer\um",
    "$sdkRoot\Include\$sdkVer\winrt",
    "$sdkRoot\Include\$sdkVer\cppwinrt"
) -join ";"

$env:LIB = @(
    "$msvcDir\lib\x64",
    "$sdkRoot\Lib\$sdkVer\ucrt\x64",
    "$sdkRoot\Lib\$sdkVer\um\x64"
) -join ";"

$env:LIBPATH = $env:LIB

# Prepend the compiler / SDK tools, keeping the existing PATH intact.
$env:PATH = @(
    "$msvcDir\bin\Hostx64\x64",
    "$sdkRoot\bin\$sdkVer\x64",
    "$vsPath\Common7\IDE",
    "$vsPath\Common7\Tools",
    $env:PATH
) -join ";"

# --- Configure + build ----------------------------------------------------

Set-Location $projectDir

$configureArgs = @(
    "-S", $projectDir,
    "-B", $buildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=cl",
    "-DCMAKE_CXX_COMPILER=cl",
    "-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSrc",
    "-DORGANIC_COPY_PLUGIN_AFTER_BUILD=OFF",
    "-DORGANIC_BUILD_BELL_TESTS=ON"
)

& $cmake @configureArgs > build.log 2>&1
$code = $LASTEXITCODE
if ($code -ne 0) { Get-Content build.log -Tail 60; exit $code }

& $cmake --build $buildDir --parallel 4 >> build.log 2>&1
$code = $LASTEXITCODE
if ($code -ne 0) { Get-Content build.log -Tail 80; exit $code }

$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
& $ctest --test-dir $buildDir -C Release -R "^OrganicChemistryBellTests$" --output-on-failure
$code = $LASTEXITCODE
if ($code -ne 0) { exit $code }

$cmakeText = Get-Content (Join-Path $projectDir "CMakeLists.txt") -Raw
$expectedVersion = [regex]::Match($cmakeText, 'project\(OrganicChemistry VERSION ([0-9.]+)').Groups[1].Value
if (-not $expectedVersion) { throw "Cannot read the plugin version from CMakeLists.txt" }
$artefacts = Join-Path $buildDir "OrganicChemistry_artefacts\Release"
foreach ($relative in @("Standalone\ChemE-Organic Chemistry.exe", "VST3\ChemE-Organic Chemistry.vst3\Contents\x86_64-win\ChemE-Organic Chemistry.vst3")) {
    $binary = Get-Item -LiteralPath (Join-Path $artefacts $relative)
    if ($binary.VersionInfo.ProductVersion -ne $expectedVersion -or $binary.VersionInfo.ProductName -ne "ChemE-Organic Chemistry") {
        throw "Stale Windows version resource: $($binary.FullName)"
    }
}
Write-Output "VERSION_OK $expectedVersion"
Write-Output "BUILD_OK"
exit 0
