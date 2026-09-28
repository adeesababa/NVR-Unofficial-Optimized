$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..").Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[16.0,17.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio 2019 C++ tools (v142) are required.' }
& "$vs\MSBuild\Current\Bin\MSBuild.exe" "$repo\tests\shadow_bones.vcxproj" /p:Configuration=Release /p:Platform=Win32 /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
& "$repo\build\tests\shadow_bones.exe"
if ($LASTEXITCODE -ne 0) { throw 'Shadow bone regression tests failed.' }
