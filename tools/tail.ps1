param(
  [Parameter(Mandatory=$true)][string]$Path,
  [int]$Tail = 0,
  [switch]$Raw
)
$fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
$sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
$text = $sr.ReadToEnd()
$sr.Close(); $fs.Close()
if ($Tail -gt 0) {
  $lines = $text -split "`r?`n"
  if ($lines.Count -gt $Tail) { $lines = $lines[($lines.Count - $Tail)..($lines.Count - 1)] }
  ($lines -join "`n")
} else {
  if ($Raw) { $text } else { $text }
}
