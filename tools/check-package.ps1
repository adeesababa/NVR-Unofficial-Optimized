# Checks a package folder against this checkout before it is zipped.
#
# Since P40 a package carries the COMPLETE shader folders of this repo (not only the files changed since
# the fork we build from), so a tester's older shader files can never mix with ours. Found 2026-09-29
# from a tester's log: Effects\Includes\Normals.hlsl had been missing from every overlay package since
# P8, so on an install without it SunShadows and VolumetricFog failed to compile. Since P45 it also
# carries all of NVR's textures (user decision), so the zip is a complete install on its own; a user
# who installed an older zip without NVR underneath crashed at startup.
#
# Checked: every shader and texture file tracked in the repo is in the package and current (text
# files line endings aside), the package holds no other shader or texture files, and the DLL and
# defaults file are the current ones.
#
#   powershell -ExecutionPolicy Bypass -File tools\check-package.ps1 ..\..\outputs\p45-package
param([Parameter(Mandatory = $true)][string]$Package)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..").Path
$pkg = (Resolve-Path $Package).Path
$folders = [ordered]@{
	'src/hlsl/NewVegas/Effects/' = 'Shaders\NewVegasReloaded\Effects\'
	'src/hlsl/NewVegas/Shaders/' = 'Shaders\NewVegasReloaded\Shaders\'
	'resource/Textures/' = 'Textures\'
}
function Text([string]$path) { [IO.File]::ReadAllText($path).Replace("`r`n", "`n") }
function Same([string]$a, [string]$b) {
	if ($a -like '*.hlsl' -or $a -like '*.disabled') { return (Text $a) -eq (Text $b) }
	return (Get-FileHash $a).Hash -eq (Get-FileHash $b).Hash
}
$problems = 0
$checked = 0

foreach ($entry in $folders.GetEnumerator()) {
	# 1. Every tracked file of the folder is shipped and current.
	$tracked = & git -C $repo -c safe.directory=* ls-files -- $entry.Key
	if ($LASTEXITCODE -ne 0) { throw "git ls-files failed" }
	foreach ($file in $tracked) {
		$checked++
		$target = Join-Path $pkg ($entry.Value + $file.Substring($entry.Key.Length).Replace('/', '\'))
		if (!(Test-Path $target)) { Write-Host "MISSING  $file"; $problems++ }
		elseif (!(Same $target (Join-Path $repo $file))) { Write-Host "STALE    $file"; $problems++ }
	}
	# 2. Nothing else is in it (a leftover file would be installed as if it were ours).
	$dir = Join-Path $pkg $entry.Value
	if (!(Test-Path $dir)) { continue }
	$known = @{}
	foreach ($file in $tracked) { $known[$file.Substring($entry.Key.Length).Replace('/', '\').ToLowerInvariant()] = $true }
	foreach ($item in Get-ChildItem $dir -Recurse -File) {
		if (!$known.ContainsKey($item.FullName.Substring($dir.Length).ToLowerInvariant())) { Write-Host "EXTRA    $($item.FullName)"; $problems++ }
	}
}

# 3. Only what players need at the top level (no working notes).
$allowed = 'COPYING.txt', 'License.md', 'README.md', 'NVSE', 'Shaders', 'Textures'
foreach ($item in Get-ChildItem $pkg) {
	if ($allowed -notcontains $item.Name) { Write-Host "EXTRA    $($item.FullName)"; $problems++ }
}
foreach ($item in Get-ChildItem (Join-Path $pkg 'NVSE\Plugins')) {
	if ('NewVegasReloaded.dll', 'NewVegasReloaded.pdb', 'NewVegasReloaded.dll.defaults.toml' -notcontains $item.Name) { Write-Host "EXTRA    $($item.FullName)"; $problems++ }
}
foreach ($file in 'COPYING.txt', 'License.md', 'README.md') {
	if (!(Test-Path (Join-Path $pkg $file))) { Write-Host "MISSING  $file"; $problems++ }
	elseif ((Text (Join-Path $pkg $file)) -ne (Text (Join-Path $repo $file))) { Write-Host "STALE    $file"; $problems++ }
}

# 4. The DLL is the latest local build and the defaults file is the repo's.
$plugins = Join-Path $pkg 'NVSE\Plugins'
if ((Get-FileHash (Join-Path $plugins 'NewVegasReloaded.dll')).Hash -ne (Get-FileHash (Join-Path $repo 'build\optimized\NewVegasReloaded.dll')).Hash) {
	Write-Host "STALE    NVSE\Plugins\NewVegasReloaded.dll (not build\optimized's)"; $problems++
}
if ((Text (Join-Path $plugins 'NewVegasReloaded.dll.defaults.toml')) -ne (Text (Join-Path $repo 'resource\NewVegasReloaded.dll.defaults.toml'))) {
	Write-Host "STALE    NVSE\Plugins\NewVegasReloaded.dll.defaults.toml"; $problems++
}

if ($problems) { throw "$problems package problem(s) in $pkg" }
Write-Host "Package OK: $pkg ($checked shader/texture files, DLL and defaults current)"
