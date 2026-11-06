# Recreate third_party/fear-publictools, the F.E.A.R. v1.08 public tools source that pc/ builds:
# unpack the public tools installer that ships with the game (extras\fear_publictools_108.exe
# in the Steam copy), keep Game/, engine/ and libs/, check them against the tree this project was
# built from, then apply pc/game/patches in order on a local branch.
#
# The source is EULA-licensed: it stays in third_party/ (gitignored, and tools/audit_repo.py
# rejects it anywhere else).
#   pwsh -NoProfile -File pc/tools/setup.ps1 -Installer <path to fear_publictools_108.exe>
#   pwsh -NoProfile -File pc/tools/setup.ps1 -Force      # replace an existing tree
[CmdletBinding()]
param(
    [string]$Installer,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$dest = Join-Path $root 'third_party\fear-publictools'
$installerSha256 = '11aaa4128528403f7bc9ea5119c68051c62b92a99e6411dfd749af55e9b19df8'
$pristineTree = '73e65f11e15ea4acf9adb282c785bd468c7d7fd1'   # Game, engine, libs as unpacked

function Invoke-Git {
    & git.exe @args
    if ($LASTEXITCODE) { throw "git $($args -join ' ') failed ($LASTEXITCODE)." }
}

if (-not $Installer) {
    $steam = Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Monolith Productions\FEAR' -ErrorAction SilentlyContinue
    foreach ($dir in @($steam.InstallDir, $steam.'Install Dir') | Where-Object { $_ }) {
        $candidate = Join-Path $dir 'extras\fear_publictools_108.exe'
        if (Test-Path $candidate) { $Installer = $candidate; break }
    }
}
if (-not $Installer -or -not (Test-Path $Installer)) {
    throw 'Pass -Installer <fear_publictools_108.exe> (the Steam copy has it in extras\).'
}
if ((Get-FileHash $Installer -Algorithm SHA256).Hash.ToLower() -ne $installerSha256) {
    throw "$Installer is not the v1.08 public tools installer this project was built from."
}
if (Test-Path $dest) {
    if (-not $Force) { throw "$dest exists already. Run with -Force to replace it." }
    Remove-Item -Recurse -Force $dest
}

$work = Join-Path ([IO.Path]::GetTempPath()) "fear-publictools-$PID"
try {
    New-Item -ItemType Directory -Force $work | Out-Null
    Write-Host 'Extracting the installer payload'
    $p = Start-Process -FilePath $Installer -ArgumentList "/extract_all:$work\payload" -PassThru -Wait
    if ($p.ExitCode -or -not (Test-Path "$work\payload\Disk1\data1.hdr")) { throw 'Installer extraction failed.' }
    Write-Host 'Unpacking the cabinets (MD5-checked)'
    & python -I (Join-Path $PSScriptRoot 'isextract.py') "$work\payload\Disk1" "$work\files"
    if ($LASTEXITCODE) { throw 'Some files failed their MD5 check.' }

    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($d in 'Game', 'engine', 'libs') { Copy-Item -Recurse (Join-Path "$work\files" $d) $dest }
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

$identity = @('-c', 'user.name=fear-pc setup', '-c', 'user.email=setup@localhost', '-c', 'core.autocrlf=false')
Invoke-Git -C $dest init --quiet -b pristine
Invoke-Git -C $dest @identity add -A
Invoke-Git -C $dest @identity commit --quiet -m 'F.E.A.R. public tools v1.08: Game, engine/sdk, libs (unmodified)'
$tree = & git -C $dest rev-parse 'HEAD^{tree}'
if ($tree -ne $pristineTree) { throw "The unpacked tree is $tree, not $pristineTree." }
Invoke-Git -C $dest switch --quiet -c fear-pc
$patches = Get-ChildItem (Join-Path $root 'pc\game\patches') -Filter '*.patch' | Sort-Object Name
if ($patches) {
    Write-Host "Applying $($patches.Count) patches"
    Invoke-Git -C $dest @identity am --quiet --keep-cr @($patches | ForEach-Object FullName)
}
Write-Host "Ready: $dest (branch fear-pc). Build with pc/tools/build.ps1."
