# Read-only, one-shot probe. Never shows/hides Explorer, invokes icons, or launches Dock.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TrayProbe {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string name);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
}
'@
$version = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$handle = [TrayProbe]::FindWindow('Shell_TrayWnd', $null)
$report = [ordered]@{
    build = $version.CurrentBuildNumber
    explorerTaskbarExists = $handle -ne [IntPtr]::Zero
    explorerTaskbarVisible = [TrayProbe]::IsWindowVisible($handle)
    automationElements = 0
    systemTrayElements = 0
    offscreenTrayElements = 0
    trayInvokeProviders = 0
    fullEnumerationSupported = $false
    hiddenTaskbarMutated = $false
}
if ($handle -ne [IntPtr]::Zero) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($handle)
    $elements = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    $report.automationElements = $elements.Count
    foreach ($element in $elements) {
        if ($element.Current.AutomationId -notmatch 'SystemTray') { continue }
        $report.systemTrayElements++
        if ($element.Current.IsOffscreen) { $report.offscreenTrayElements++ }
        $pattern = $null
        if ($element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) { $report.trayInvokeProviders++ }
    }
}
$report | ConvertTo-Json
