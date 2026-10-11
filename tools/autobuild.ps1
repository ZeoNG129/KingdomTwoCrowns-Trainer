param(
  [string]$Dir = 'left',
  [int]$Seconds = 4,
  [int]$TapMs = 300
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class KtcAuto {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KEYBDINPUT ki; public static int Size { get { return Marshal.SizeOf(typeof(INPUT)); } } }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  public static void Key(ushort scan, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = 1;
    a[0].ki.wScan = scan;
    a[0].ki.dwFlags = 8 | (up ? 2u : 0u);
    SendInput(1, a, INPUT.Size);
  }
  public static bool Focus() {
    foreach (var p in System.Diagnostics.Process.GetProcessesByName("KingdomTwoCrowns")) {
      IntPtr h = p.MainWindowHandle;
      if (h == IntPtr.Zero) continue;
      ShowWindow(h, 9);
      uint pid; uint fg = GetWindowThreadProcessId(GetForegroundWindow(), out pid);
      uint cur = GetCurrentThreadId();
      AttachThreadInput(fg, cur, true);
      bool ok = SetForegroundWindow(h);
      AttachThreadInput(fg, cur, false);
      return ok;
    }
    return false;
  }
}
"@
$moveScan = if ($Dir -eq 'left') { [uint16]0x4B } else { [uint16]0x4D }
"focus: $([KtcAuto]::Focus())"
Start-Sleep -Milliseconds 500
[KtcAuto]::Key($moveScan, $false)
Start-Sleep -Milliseconds 250
$end = (Get-Date).AddSeconds($Seconds)
$n = 0
while ((Get-Date) -lt $end) {
  [KtcAuto]::Key([uint16]0x50, $false)   # Down down
  Start-Sleep -Milliseconds 60
  [KtcAuto]::Key([uint16]0x50, $true)    # Down up
  $n++
  Start-Sleep -Milliseconds $TapMs
}
[KtcAuto]::Key($moveScan, $true)
"moved $Dir for ${Seconds}s, tapped Down $n times"
