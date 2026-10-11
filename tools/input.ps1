param(
  [ValidateSet('left','right','up','down','space','enter','esc','f1','f2','f3','f4','f5','f6','f7','f8','f9','f10','f11','shift','a','d','w','s')]
  [string]$Key = 'right',
  [int]$HoldMs = 400,
  [ValidateSet('tap','down','up')][string]$Action = 'tap',
  [string]$Click = '',
  [switch]$Focus
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class KtcInput {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string win);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public InputUnion u; public static int Size { get { return Marshal.SizeOf(typeof(INPUT)); } } }
  [StructLayout(LayoutKind.Explicit)] public struct InputUnion { [FieldOffset(0)] public KEYBDINPUT ki; [FieldOffset(0)] public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  public const uint INPUT_KEYBOARD = 1, INPUT_MOUSE = 0, KEYEVENTF_KEYUP = 2, KEYEVENTF_SCANCODE = 8;
  public const uint MOUSEEVENTF_MOVE = 1, MOUSEEVENTF_ABSOLUTE = 0x8000, MOUSEEVENTF_LEFTDOWN = 2, MOUSEEVENTF_LEFTUP = 4;
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  public static void Send(ushort vk, ushort scan, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = INPUT_KEYBOARD;
    a[0].u.ki.wVk = vk; a[0].u.ki.wScan = scan;
    a[0].u.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, a, INPUT.Size);
  }
  public static void ClickAt(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(120);
    INPUT[] a = new INPUT[2];
    a[0].type = INPUT_MOUSE; a[0].u.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    a[1].type = INPUT_MOUSE; a[1].u.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, a, INPUT.Size);
  }
  public static void FocusGame(string procName) {
    System.Diagnostics.Process p = null;
    foreach (var q in System.Diagnostics.Process.GetProcessesByName(procName)) { p = q; break; }
    if (p == null) return;
    IntPtr h = p.MainWindowHandle;
    if (h == IntPtr.Zero) return;
    ShowWindow(h, 9);
    uint pid;
    uint fg = GetWindowThreadProcessId(GetForegroundWindow(), out pid);
    uint cur = GetCurrentThreadId();
    AttachThreadInput(fg, cur, true);
    SetForegroundWindow(h);
    AttachThreadInput(fg, cur, false);
  }
}
"@
$map = @{
  left=0x4B; right=0x4D; up=0x48; down=0x50; space=0x39; enter=0x1C; esc=0x01;
  shift=0x2A; a=0x1E; d=0x20; w=0x11; s=0x1F;
  f1=0x3B; f2=0x3C; f3=0x3D; f4=0x3E; f5=0x3F; f6=0x40; f7=0x41; f8=0x42; f9=0x43; f10=0x44; f11=0x57
}
if ($Focus) { [KtcInput]::FocusGame("KingdomTwoCrowns"); Start-Sleep -Milliseconds 400 }
if ($Click -ne '') {
  $parts = $Click -split ','
  [KtcInput]::ClickAt([int]$parts[0], [int]$parts[1])
  "clicked: $Click"
  exit 0
}
$scan = [uint16]$map[$Key]
if ($Action -eq 'down' -or $Action -eq 'tap') { [KtcInput]::Send(0, $scan, $false) }
if ($Action -eq 'tap') { Start-Sleep -Milliseconds $HoldMs }
if ($Action -eq 'up' -or $Action -eq 'tap') { [KtcInput]::Send(0, $scan, $true) }
"sent: $Key ($Action, scan 0x{0:X2})" -f $scan
