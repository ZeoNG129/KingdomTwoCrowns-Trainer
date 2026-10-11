param(
  [string]$Out = "",
  [int]$MaxWidth = 0,
  [int]$DelayMs = 250,
  [switch]$NoFocus
)
if (-not $Out) { $Out = Join-Path (Split-Path $PSScriptRoot -Parent) "logs\game.png" }
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class KtcWin {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static IntPtr GameHwnd(string proc) {
    foreach (var p in System.Diagnostics.Process.GetProcessesByName(proc)) {
      if (p.MainWindowHandle != IntPtr.Zero) return p.MainWindowHandle;
    }
    return IntPtr.Zero;
  }
  public static uint PidOf(IntPtr h) { uint pid; GetWindowThreadProcessId(h, out pid); return pid; }
  public static bool FocusGame(string proc) {
    IntPtr h = GameHwnd(proc);
    if (h == IntPtr.Zero) return false;
    ShowWindow(h, 9);
    uint pid; uint fg = GetWindowThreadProcessId(GetForegroundWindow(), out pid);
    uint cur = GetCurrentThreadId();
    AttachThreadInput(fg, cur, true);
    BringWindowToTop(h);
    bool ok = SetForegroundWindow(h);
    AttachThreadInput(fg, cur, false);
    return ok;
  }
  public static bool IsGameFocused(string proc) {
    IntPtr h = GetForegroundWindow();
    if (h == IntPtr.Zero) return false;
    uint pid = PidOf(h);
    foreach (var p in System.Diagnostics.Process.GetProcessesByName(proc)) if ((uint)p.Id == pid) return true;
    return false;
  }
  public static int[] GameRect(string proc) {
    IntPtr h = GameHwnd(proc);
    if (h == IntPtr.Zero) return null;
    RECT r; GetWindowRect(h, out r);
    return new int[] { r.L, r.T, r.R - r.L, r.B - r.T };
  }
}
"@
$proc = "KingdomTwoCrowns"
$focused = $false
if (-not $NoFocus) {
  for ($i = 0; $i -lt 6; $i++) {
    [KtcWin]::FocusGame($proc) | Out-Null
    Start-Sleep -Milliseconds 220
    if ([KtcWin]::IsGameFocused($proc)) { $focused = $true; break }
  }
}
Start-Sleep -Milliseconds $DelayMs
$rect = [KtcWin]::GameRect($proc)
if (-not $rect) { Write-Error "game window not found"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap($rect[2], $rect[3])
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect[0], $rect[1], 0, 0, (New-Object System.Drawing.Size($rect[2], $rect[3])))
$g.Dispose()
if ($MaxWidth -gt 0 -and $rect[2] -gt $MaxWidth) {
  $h = [int]($rect[3] * $MaxWidth / $rect[2])
  $small = New-Object System.Drawing.Bitmap($MaxWidth, $h)
  $g2 = [System.Drawing.Graphics]::FromImage($small)
  $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g2.DrawImage($bmp, 0, 0, $MaxWidth, $h); $g2.Dispose(); $bmp.Dispose(); $bmp = $small
}
$dir = Split-Path $Out -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved: $Out  rect=$($rect[0]),$($rect[1]),$($rect[2]),$($rect[3])  focused=$focused"
