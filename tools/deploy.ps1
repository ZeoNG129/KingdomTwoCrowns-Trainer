param(
  [string]$Dll = "ktc_mod_build.dll",
  [string]$Proc = "KingdomTwoCrowns.exe",
  [switch]$NoInject
)
$root = Split-Path $PSScriptRoot -Parent   # 工程根 = tools 的上级
$src = Join-Path $root "build\$Dll"
if (-not (Test-Path $src)) { Write-Error "not found: $src"; exit 1 }

# unique file name per deploy so LoadLibrary actually loads the new build
$runDir = Join-Path $root "build\run"
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$stamp = Get-Date -Format "HHmmss"
$name = [System.IO.Path]::GetFileNameWithoutExtension($Dll)
$dst = Join-Path $runDir ("{0}_{1}.dll" -f $name, $stamp)
Copy-Item $src $dst -Force
"deployed: $dst"
if ($NoInject) { exit 0 }
& (Join-Path $root "build\inject.exe") --name $Proc --dll $dst
exit $LASTEXITCODE
