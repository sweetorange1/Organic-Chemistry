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

$projectDir = "I:\Organic Chemistry"
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
    "-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSrc"
)

& $cmake @configureArgs > build.log 2>&1
if ($LASTEXITCODE -ne 0) { Get-Content build.log -Tail 60; exit $LASTEXITCODE }

& $cmake --build $buildDir >> build.log 2>&1
if ($LASTEXITCODE -ne 0) { Get-Content build.log -Tail 80; exit $LASTEXITCODE }

Write-Output "BUILD_OK"
exit 0
