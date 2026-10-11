param(
  [Parameter(Mandatory=$true)][string]$Text,
  [string]$Root = "",
  [int]$Tail = 25
)
# 默认工程根 = 本脚本所在目录的上级
if (-not $Root) { $Root = Split-Path $PSScriptRoot -Parent }
$cmd = Join-Path $Root "dump\cmd.txt"
$dumpDir = Split-Path $cmd -Parent
if (-not (Test-Path $dumpDir)) { New-Item -ItemType Directory -Path $dumpDir -Force | Out-Null }
# unique content each time so the mod always re-processes it
$stamp = Get-Date -Format "HH:mm:ss.fff"
Set-Content -Path $cmd -Value "# $stamp`n$Text" -Encoding ASCII
Start-Sleep -Milliseconds 400
powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "tail.ps1") -Path (Join-Path $Root "logs\mod.log") -Tail $Tail
