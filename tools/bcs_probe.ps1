<#
    bcs_probe.ps1 -- reads the shape of the _connect.BRAIN automation objects.

    Read-only by construction. It creates the automation objects and asks them
    to describe themselves; it never calls Open, Send, Receive or any FTP
    method, so no device connection is established and nothing on the line is
    disturbed. Creating the object only starts (or attaches to) the COM server.

    Usage:
        powershell -ExecutionPolicy Bypass -File bcs_probe.ps1
        powershell -ExecutionPolicy Bypass -File bcs_probe.ps1 -Out report.txt

    Run it in the bitness that matches the registration. The objects are
    registered in the 64-bit view, so use the 64-bit PowerShell
    (C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe), not the
    SysWOW64 one.
#>

param(
    [string]$Out = "bcs_probe_report.txt"
)

$ErrorActionPreference = 'Continue'
$lines = New-Object System.Collections.Generic.List[string]

function Emit($text = '') {
    $lines.Add($text) | Out-Null
    Write-Host $text
}

function Section($title) {
    Emit ''
    Emit ('=' * 78)
    Emit "  $title"
    Emit ('=' * 78)
}

Section "Environment"
Emit "Date            : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Emit "Computer        : $env:COMPUTERNAME"
Emit "PowerShell      : $($PSVersionTable.PSVersion)"
Emit "Process is 64bit: $([Environment]::Is64BitProcess)"
Emit "OS is 64bit     : $([Environment]::Is64BitOperatingSystem)"

# ---------------------------------------------------------------------------
# 1. Registration, read straight from the registry. No object is created here.
# ---------------------------------------------------------------------------

$progIds = @(
    'BCS.BCSComunnication.1',   # note the vendor's spelling: one m, two n
    'BCS.BCSComunnication.2',
    'BCS.BCSComunnication.3',
    'BCS.BCSCommunication.1',   # the spelling the manual uses; expected absent
    'BCS.BCSInfo.1',
    'BCS.BCSConfig.1',
    'BCS.BCSDiag.1',
    'BCS.BCSBcf.1',
    'BCS.BCSLw.1',
    'BCC.BCCCtrl.1'
)

Section "Registration"

foreach ($progId in $progIds) {
    $clsidKey = "Registry::HKEY_CLASSES_ROOT\$progId\CLSID"
    if (-not (Test-Path $clsidKey)) {
        Emit ("{0,-26} not registered" -f $progId)
        continue
    }

    $clsid = (Get-ItemProperty $clsidKey).'(default)'
    Emit ("{0,-26} {1}" -f $progId, $clsid)

    $base = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid"
    foreach ($kind in 'InprocServer32', 'LocalServer32') {
        $key = "$base\$kind"
        if (Test-Path $key) {
            $server = (Get-ItemProperty $key).'(default)'
            Emit ("{0,-26}   {1}: {2}" -f '', $kind, $server)
        }
    }

    # The type library is what carries the real signatures.
    $tlbKey = "$base\TypeLib"
    if (Test-Path $tlbKey) {
        $tlb = (Get-ItemProperty $tlbKey).'(default)'
        Emit ("{0,-26}   TypeLib: {1}" -f '', $tlb)
        $tlbRoot = "Registry::HKEY_CLASSES_ROOT\TypeLib\$tlb"
        if (Test-Path $tlbRoot) {
            Get-ChildItem $tlbRoot | ForEach-Object {
                $ver = $_.PSChildName
                Get-ChildItem "$tlbRoot\$ver" -ErrorAction SilentlyContinue |
                    Where-Object { $_.PSChildName -match '^(win32|win64)$' } |
                    ForEach-Object {
                        $path = (Get-ItemProperty $_.PSPath).'(default)'
                        Emit ("{0,-26}   tlb v{1} {2}: {3}" -f '', $ver, $_.PSChildName, $path)
                    }
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 2. Member signatures. Creating the object is safe; we call nothing on it.
# ---------------------------------------------------------------------------

$probeTargets = @(
    'BCS.BCSInfo.1',            # documented as opening no device connection
    'BCS.BCSComunnication.1',
    'BCS.BCSComunnication.2',
    'BCS.BCSComunnication.3',
    'BCC.BCCCtrl.1'
)

foreach ($progId in $probeTargets) {
    Section "Members of $progId"

    $obj = $null
    try {
        $obj = New-Object -ComObject $progId -ErrorAction Stop
    } catch {
        Emit "could not create: $($_.Exception.Message)"
        continue
    }

    try {
        Emit "COM type name: $($obj.GetType().FullName)"
        Emit ''

        $members = $obj | Get-Member -Force | Sort-Object MemberType, Name
        foreach ($m in $members) {
            Emit ("{0,-10} {1}" -f $m.MemberType, $m.Definition)
        }

        # Interface IIDs the object answers to -- useful when deciding whether
        # to bind early or late.
        try {
            $iface = [System.Runtime.InteropServices.Marshal]::GetIDispatchForObject($obj)
            if ($iface -ne [IntPtr]::Zero) {
                Emit ''
                Emit 'IDispatch: yes (late binding usable)'
                [System.Runtime.InteropServices.Marshal]::Release($iface) | Out-Null
            }
        } catch {
            Emit ''
            Emit "IDispatch: probe failed -- $($_.Exception.Message)"
        }
    } finally {
        # Release promptly so no server is left holding a client slot.
        [System.Runtime.InteropServices.Marshal]::ReleaseComObject($obj) | Out-Null
        $obj = $null
        [System.GC]::Collect()
        [System.GC]::WaitForPendingFinalizers()
    }
}

Section "Done"
Emit "No Open / Send / Receive / FTP method was called."

# UTF-8 without BOM keeps the file readable on the Mac side.
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines((Join-Path (Get-Location) $Out), $lines, $utf8)
Write-Host ''
Write-Host "Report written to $(Join-Path (Get-Location) $Out)"
