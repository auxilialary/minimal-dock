param()
$ErrorActionPreference = 'Stop'
if (Get-Process Dock -ErrorAction SilentlyContinue) { throw 'Close existing Dock first.' }
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DockBenchNative {
 public delegate bool WindowCallback(IntPtr hwnd, IntPtr param);
 [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr param);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 public static void CloseOwned(int process) {
  EnumWindows((hwnd,param)=> { uint pid; GetWindowThreadProcessId(hwnd,out pid);
   if(pid==process) PostMessage(hwnd,0x10,IntPtr.Zero,IntPtr.Zero); return true; },IntPtr.Zero);
 }

 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
}
"@
$exe = (Resolve-Path "$PSScriptRoot\..\x64\Release\Dock.exe").Path
for ($run=1; $run -le 3; $run++) {
 $tray = [DockBenchNative]::FindWindow('Shell_TrayWnd',$null)
 if (!$tray -or ![DockBenchNative]::IsWindowVisible($tray)) { throw 'Visible native taskbar required.' }
 $info = [Diagnostics.ProcessStartInfo]::new($exe)
 $info.UseShellExecute=$false
 $info.EnvironmentVariables['GLASSSPLIT_PERF']='1'
 $info.EnvironmentVariables['GLASSSPLIT_DIAGNOSTICS']='1'
 $dock = [Diagnostics.Process]::Start($info)
 try {
  for ($i=0;$i -lt 12;$i++) {
   if ($dock.HasExited) { throw 'Dock exited during startup.' }
   if (![DockBenchNative]::IsWindowVisible($tray)) { throw 'Taskbar visibility lost.' }
   Start-Sleep -Milliseconds 500
  }
  & "$PSScriptRoot\measure-process-tree.ps1" -DockProcessId $dock.Id -Seconds 8 -Scenario "warm-$run-idle" -Output "$PSScriptRoot\warm-$run-idle.csv"
  # Duplicate launch inherits the same taskbar-safe flags and must exit promptly.
  $duplicate=[Diagnostics.Process]::Start($info)
  if (!$duplicate.WaitForExit(5000)) { $duplicate.Kill(); throw 'Duplicate did not exit.' }
 } finally {
  if (!$dock.HasExited) { [DockBenchNative]::CloseOwned($dock.Id) }
  if (!$dock.WaitForExit(15000)) { $dock.Kill($true); throw 'Graceful exit timed out; test tree terminated.' }
  $trace=Join-Path $env:LOCALAPPDATA "GlassSplit\performance-$($dock.Id).tsv"
  if (Test-Path $trace) { Copy-Item $trace "$PSScriptRoot\warm-$run.tsv" }
 }
 "Run $run completed PID=$($dock.Id), exit=$($dock.ExitCode)"
}
