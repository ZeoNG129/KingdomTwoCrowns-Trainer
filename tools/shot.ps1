param(
  [string]$Out = "",
  [int]$MaxWidth = 1600
)
if (-not $Out) { $Out = Join-Path (Split-Path $PSScriptRoot -Parent) "logs\screen.png" }
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$g.Dispose()
if ($MaxWidth -gt 0 -and $b.Width -gt $MaxWidth) {
  $h = [int]($b.Height * $MaxWidth / $b.Width)
  $small = New-Object System.Drawing.Bitmap($MaxWidth, $h)
  $g2 = [System.Drawing.Graphics]::FromImage($small)
  $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g2.DrawImage($bmp, 0, 0, $MaxWidth, $h)
  $g2.Dispose()
  $bmp.Dispose()
  $bmp = $small
}
$dir = Split-Path $Out -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved: $Out  ($((Get-Item $Out).Length) bytes)"
