param([string]$Distribution = 'Ubuntu-22.04', [switch]$Doctor)
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path -Parent $PSScriptRoot
# Forward slashes survive WSL's argument forwarding without losing backslashes.
$linuxDir = & wsl.exe -d $Distribution -- wslpath -a -u $repoDir.Replace('\', '/')
if ($LASTEXITCODE -ne 0) { throw 'Unable to resolve the repository in WSL.' }
$buildArgs = @('-d', $Distribution, '--', 'sh', "$($linuxDir.Trim())/tools/build_rgb30.sh")
if ($Doctor) { $buildArgs += '--doctor' }
& wsl.exe @buildArgs
if ($LASTEXITCODE -ne 0) { throw "RGB30 build/check failed with exit code $LASTEXITCODE." }
