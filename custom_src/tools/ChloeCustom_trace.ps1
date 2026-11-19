# Starts Fouc.exe from the folder this script is in (or -GameDir) and records for 20 s every change of the game
# window (rect, client size, style, DPI) and of the game's display settings in memory (0x764A08 block = device.cfg).
# Output: ChloeCustom_trace.txt in the game folder. Steam must be running. Windows PowerShell 5.1 compatible.
param([string]$GameDir = $PSScriptRoot, [int]$Seconds = 20)
$out = Join-Path $GameDir 'ChloeCustom_trace.txt'
$log = New-Object System.Collections.Generic.List[string]
function W($s) { $log.Add(('{0,7:N0} ms  ' -f $sw.Elapsed.TotalMilliseconds) + $s) }

Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public class TrW {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetAwarenessFromDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint f);
  [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO mi);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, int n, out IntPtr r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public int dwFlags; }
  public static int ReadInt(IntPtr p, long a) { byte[] b = new byte[4]; IntPtr r; return ReadProcessMemory(p, new IntPtr(a), b, 4, out r) ? BitConverter.ToInt32(b, 0) : int.MinValue; }
}
'@
# measure in physical pixels, independent of how this PowerShell is scaled
[void][TrW]::SetThreadDpiAwarenessContext([IntPtr](-4))   # PER_MONITOR_AWARE_V2

$sw = [Diagnostics.Stopwatch]::StartNew()
$exe = Join-Path $GameDir 'Fouc.exe'
if (Get-Process Fouc -ErrorAction SilentlyContinue) { Write-Host 'Fouc.exe laeuft schon - bitte zuerst beenden.'; pause; exit 1 }
W ("Start " + $exe + "  (" + (Get-Date) + ", " + $env:COMPUTERNAME + ")")
$p = Start-Process -FilePath $exe -WorkingDirectory $GameDir -PassThru
$h = [TrW]::OpenProcess(0x0410, $false, $p.Id)   # VM_READ | QUERY_INFORMATION
$last = ''
$deadline = $Seconds * 1000
while ($sw.Elapsed.TotalMilliseconds -lt $deadline -and -not $p.HasExited) {
  $hwnd = [IntPtr][TrW]::ReadInt($h, 0x7242B0 + 0x7C)
  $mem = 'mem res={0}x{1} bpp={2} hz={3} setupWindowed={4}' -f [TrW]::ReadInt($h, 0x764A84), [TrW]::ReadInt($h, 0x764A88), [TrW]::ReadInt($h, 0x764A8C), [TrW]::ReadInt($h, 0x764A90), [TrW]::ReadInt($h, 0x764AAC)
  $win = 'no window'
  if ($hwnd -ne [IntPtr]::Zero -and [TrW]::IsWindow($hwnd)) {
    $r = New-Object TrW+RECT; [void][TrW]::GetWindowRect($hwnd, [ref]$r)
    $c = New-Object TrW+RECT; [void][TrW]::GetClientRect($hwnd, [ref]$c)
    $mi = New-Object TrW+MONITORINFO; $mi.cbSize = 40; [void][TrW]::GetMonitorInfo([TrW]::MonitorFromWindow($hwnd, 2), [ref]$mi)
    $aw = [TrW]::GetAwarenessFromDpiAwarenessContext([TrW]::GetWindowDpiAwarenessContext($hwnd))
    $win = 'win rect=({0},{1})-({2},{3}) size={4}x{5} client={6}x{7} style=0x{8:X8} visible={9} iconic={10} dpi={11} awareness={12} monitor={13}x{14} work={15}x{16}' -f `
      $r.L, $r.T, $r.R, $r.B, ($r.R - $r.L), ($r.B - $r.T), ($c.R - $c.L), ($c.B - $c.T), [TrW]::GetWindowLong($hwnd, -16), `
      [TrW]::IsWindowVisible($hwnd), [TrW]::IsIconic($hwnd), [TrW]::GetDpiForWindow($hwnd), $aw, `
      ($mi.rcMonitor.R - $mi.rcMonitor.L), ($mi.rcMonitor.B - $mi.rcMonitor.T), ($mi.rcWork.R - $mi.rcWork.L), ($mi.rcWork.B - $mi.rcWork.T)
  }
  $now = "$mem | $win"
  if ($now -ne $last) { W $now; $last = $now }
  Start-Sleep -Milliseconds 15
}
if (-not $p.HasExited) {
  # adapter list of the game (0x84927C..0x849280, 40-byte entries, first dword = adapter object)
  function RB([long]$a, [int]$n) { $b = New-Object byte[] $n; $r = [IntPtr]::Zero; [void][TrW]::ReadProcessMemory($h, [IntPtr]$a, $b, $n, [ref]$r); $b }
  function Hex($b) { ($b | ForEach-Object { $_.ToString('X2') }) -join '' }
  W ("config adapterGuid=" + (Hex (RB 0x764A70 16)) + " adapterIndex=" + [TrW]::ReadInt($h, 0x764A80) + " field98=" + [TrW]::ReadInt($h, 0x764A98) + " selectedAdapter(0x8464A8)=" + [TrW]::ReadInt($h, 0x8464A8))
  $first = [TrW]::ReadInt($h, 0x84927C); $end = [TrW]::ReadInt($h, 0x849280)
  $n = [int](($end - $first) / 40); W "adapters: $n"
  for ($i = 0; $i -lt $n -and $i -lt 8; $i++) {
    $ad = [TrW]::ReadInt($h, $first + 40 * $i)
    $name = [Text.Encoding]::ASCII.GetString((RB $ad 512)).Split([char]0)[0]
    $ml = [TrW]::ReadInt($h, $ad + 0x34C); $me = [TrW]::ReadInt($h, $ad + 0x350); $mc = [int](($me - $ml) / 4)
    W ("adapter[{0}] obj=0x{1:X} name='{2}' idx204={3} guid={4} flags33c=0x{5:X} modes={6}" -f $i, $ad, $name, [TrW]::ReadInt($h, $ad + 0x204), (Hex (RB ($ad + 0x358) 16)), [TrW]::ReadInt($h, $ad + 0x33C), $mc)
    for ($m = 0; $m -lt $mc; $m++) {
      $mo = [TrW]::ReadInt($h, $ml + 4 * $m); $w = [TrW]::ReadInt($h, $mo + 4); $hh = [TrW]::ReadInt($h, $mo + 8)
      if ($w -ge 1920) {
        $cnt = [TrW]::ReadInt($h, $mo + 0x14); $hz = @(); for ($k = 0; $k -lt $cnt -and $k -lt 16; $k++) { $hz += [TrW]::ReadInt($h, $mo + 0x18 + 4 * $k) }
        W ("   mode {0}x{1} bpp={2} mask=0x{3:X} hz={4}" -f $w, $hh, [TrW]::ReadInt($h, $mo + 0xC), [TrW]::ReadInt($h, $mo + 0x10), ($hz -join ','))
      }
    }
  }
}
W "=== Windows display devices (EnumDisplayDevices index -> name, active)"
Add-Type @'
using System; using System.Runtime.InteropServices;
public class TrD {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct DD { public int cb; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string Name; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Str; public int Flags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Id; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Key; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumDisplayDevicesW(IntPtr dev, int i, ref DD dd, int f);
  public static string Get(int i) { DD d = new DD(); d.cb = Marshal.SizeOf(typeof(DD)); if (!EnumDisplayDevicesW(IntPtr.Zero, i, ref d, 0)) return null; return d.Name + " '" + d.Str + "' flags=0x" + d.Flags.ToString("X") + " active=" + ((d.Flags & 1) != 0); }
}
'@
for ($i = 0; $i -lt 8; $i++) { $s = [TrD]::Get($i); if ($s -eq $null) { break }; W ("display[{0}] {1}" -f $i, $s) }
W ("Ende, Spiel laeuft weiter: " + (-not $p.HasExited))
$log | Set-Content -Path $out -Encoding UTF8
Write-Host "Fertig, Protokoll: $out"
