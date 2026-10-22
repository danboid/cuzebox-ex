param(
    [Parameter(Mandatory=$true)][string]$ServerRoot,
    [Parameter(Mandatory=$true)][string]$WorkDir,
    [string]$Rom,
    [string]$Emulator = (Join-Path $PSScriptRoot '..\_obj_\uzenet\linux\cuzebox'),
    [string]$Distribution = 'Ubuntu-22.04',
    [ValidateRange(1,4)][int]$Players = 2,
    [switch]$Headless
)
$ErrorActionPreference = 'Stop'
function Convert-ToWslPath([string]$Path) {
    $absolute = [System.IO.Path]::GetFullPath($Path).Replace("\", "/")
    $translated = & wsl.exe -d $Distribution -- wslpath -a -u $absolute
    if ($LASTEXITCODE -ne 0) { throw "Cannot translate path: $absolute" }
    return ($translated | Out-String).Trim()
}
$arguments = @('-d', $Distribution, '--', 'python3',
    (Convert-ToWslPath (Join-Path $PSScriptRoot 'uzenet_local.py')),
    '--server-root', (Convert-ToWslPath $ServerRoot),
    '--work-dir', (Convert-ToWslPath $WorkDir),
    '--players', "$Players")
if ($Rom) {
    $arguments += @('--emulator', (Convert-ToWslPath $Emulator),
                    '--rom', (Convert-ToWslPath $Rom))
}
if ($Headless) { $arguments += '--headless' }
# Foreground lifetime belongs to the terminal; Ctrl+C shuts down the test group.
& wsl.exe @arguments
exit $LASTEXITCODE
