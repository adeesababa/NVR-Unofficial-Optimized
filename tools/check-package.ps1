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
	$tracked = & git -C $repo -c safe.directory=* ls-files -- $entry.Key
	if ($LASTEXITCODE -ne 0) { throw "git ls-files failed" }
	foreach ($file in $tracked) {
		$checked++
		$target = Join-Path $pkg ($entry.Value + $file.Substring($entry.Key.Length).Replace('/', '\'))
		if (!(Test-Path $target)) { Write-Host "MISSING  $file"; $problems++ }
		elseif (!(Same $target (Join-Path $repo $file))) { Write-Host "STALE    $file"; $problems++ }
	}
	$dir = Join-Path $pkg $entry.Value
	if (!(Test-Path $dir)) { continue }
	$known = @{}
	foreach ($file in $tracked) { $known[$file.Substring($entry.Key.Length).Replace('/', '\').ToLowerInvariant()] = $true }
	foreach ($item in Get-ChildItem $dir -Recurse -File) {
		if (!$known.ContainsKey($item.FullName.Substring($dir.Length).ToLowerInvariant())) { Write-Host "EXTRA    $($item.FullName)"; $problems++ }
	}
}

$allowed = 'COPYING.txt', 'License.md', 'README.md', 'NVSE', 'Shaders', 'Textures', 'Meshes', 'GunFX-README.txt'
foreach ($item in Get-ChildItem $pkg) {
	if ($allowed -notcontains $item.Name) { Write-Host "EXTRA    $($item.FullName)"; $problems++ }
}
foreach ($item in Get-ChildItem (Join-Path $pkg 'NVSE\Plugins')) {
	if ('NewVegasReloaded.dll', 'NewVegasReloaded.pdb', 'NewVegasReloaded.dll.defaults.toml', 'GunFX.dll', 'GunFX.pdb', 'GunFX.ini.defaults' -notcontains $item.Name) { Write-Host "EXTRA    $($item.FullName)"; $problems++ }
}
foreach ($file in 'COPYING.txt', 'License.md', 'README.md') {
	if (!(Test-Path (Join-Path $pkg $file))) { Write-Host "MISSING  $file"; $problems++ }
	elseif ((Text (Join-Path $pkg $file)) -ne (Text (Join-Path $repo $file))) { Write-Host "STALE    $file"; $problems++ }
}

$plugins = Join-Path $pkg 'NVSE\Plugins'
if ((Get-FileHash (Join-Path $plugins 'NewVegasReloaded.dll')).Hash -ne (Get-FileHash (Join-Path $repo 'build\optimized\NewVegasReloaded.dll')).Hash) {
	Write-Host "STALE    NVSE\Plugins\NewVegasReloaded.dll (not build\optimized's)"; $problems++
}
if ((Text (Join-Path $plugins 'NewVegasReloaded.dll.defaults.toml')) -ne (Text (Join-Path $repo 'resource\NewVegasReloaded.dll.defaults.toml'))) {
	Write-Host "STALE    NVSE\Plugins\NewVegasReloaded.dll.defaults.toml"; $problems++
}

if ((Get-FileHash (Join-Path $plugins 'GunFX.dll')).Hash -ne (Get-FileHash (Join-Path $repo 'GunFX\build\GunFX.dll')).Hash) {
	Write-Host "STALE    NVSE\Plugins\GunFX.dll (not GunFX\build's)"; $problems++
}
if ((Text (Join-Path $plugins 'GunFX.ini.defaults')) -ne (Text (Join-Path $repo 'GunFX\config\GunFX.ini.defaults'))) { Write-Host "STALE    GunFX.ini.defaults"; $problems++ }
if (Test-Path (Join-Path $plugins 'GunFX.ini')) { Write-Host "EXTRA    NVSE\Plugins\GunFX.ini (players' settings must not be shipped)"; $problems++ }
if ((Text (Join-Path $pkg 'GunFX-README.txt')) -ne (Text (Join-Path $repo 'docs\GUNFX.md'))) { Write-Host "STALE    GunFX-README.txt"; $problems++ }
$meshes = & git -C $repo -c safe.directory=* ls-files -- 'GunFX/assets/Meshes/'
foreach ($file in $meshes) {
	$target = Join-Path $pkg ('Meshes\' + $file.Substring('GunFX/assets/Meshes/'.Length).Replace('/', '\'))
	if (!(Test-Path $target)) { Write-Host "MISSING  $file"; $problems++ }
	elseif ((Get-FileHash $target).Hash -ne (Get-FileHash (Join-Path $repo $file)).Hash) { Write-Host "STALE    $file"; $problems++ }
}
if ((Get-ChildItem (Join-Path $pkg 'Meshes') -Recurse -File).Count -ne @($meshes).Count) { Write-Host "EXTRA    files in Meshes"; $problems++ }

if ($problems) { throw "$problems package problem(s) in $pkg" }
Write-Host "Package OK: $pkg ($checked shader/texture files, DLL and defaults current)"
