param(
    [Parameter(Mandatory = $true)]
    [string] $PackageDirectory
)

$ErrorActionPreference = "Stop"

function Invoke-ComMethod {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)] [string] $Name,
        [AllowNull()] [object[]] $Arguments
    )

    return $Object.GetType().InvokeMember(
        $Name,
        [Reflection.BindingFlags]::InvokeMethod,
        $null,
        $Object,
        $Arguments)
}

function Read-ComField {
    param(
        [Parameter(Mandatory = $true)] $Record,
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [int] $Index
    )

    return $Record.GetType().InvokeMember(
        $Name,
        [Reflection.BindingFlags]::GetProperty,
        $null,
        $Record,
        @($Index))
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)] $Actual,
        [Parameter(Mandatory = $true)] $Expected,
        [Parameter(Mandatory = $true)] [string] $Message
    )

    if ($Actual -ne $Expected) {
        throw "$Message (expected '$Expected', got '$Actual')"
    }
}

function Read-Record {
    param(
        [Parameter(Mandatory = $true)] $Database,
        [Parameter(Mandatory = $true)] [string] $Query
    )

    # Windows Installer accepts a deliberately small SQL dialect. In
    # particular, OpenView rejects embedded newlines even though PowerShell
    # here-strings make multi-clause queries much easier to audit below.
    $sql = ($Query -replace '\s+', ' ').Trim()
    $view = Invoke-ComMethod $Database "OpenView" @($sql)
    try {
        [void](Invoke-ComMethod $view "Execute" $null)
        return Invoke-ComMethod $view "Fetch" $null
    }
    finally {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
    }
}

$packages = @(Get-ChildItem -LiteralPath $PackageDirectory -Filter *.msi -File)
Assert-Equal $packages.Count 1 "Expected exactly one Windows installer"

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = Invoke-ComMethod $installer "OpenDatabase" `
    @($packages[0].FullName, 0)

try {
    $launchText = Read-Record $database @'
SELECT `Value` FROM `Property`
WHERE `Property`='WIXUI_EXITDIALOGOPTIONALCHECKBOXTEXT'
'@
    Assert-Equal (Read-ComField $launchText "StringData" 1) `
        "Launch CW Assistant" `
        "The finish-page launch choice is missing"

    $launchTarget = Read-Record $database @'
SELECT `Value` FROM `Property` WHERE `Property`='WixShellExecTarget'
'@
    Assert-Equal (Read-ComField $launchTarget "StringData" 1) `
        "[INSTALL_ROOT]bin\cw-assistant-desktop.exe" `
        "The launch action does not target the installed executable"

    $uncheckedDefault = Read-Record $database @'
SELECT `Value` FROM `Property`
WHERE `Property`='WIXUI_EXITDIALOGOPTIONALCHECKBOX'
'@
    if ($null -ne $uncheckedDefault) {
        throw "The launch checkbox must not have an unconditional default"
    }

    $unsafeUiCloseAction = Read-Record $database @'
SELECT `Action` FROM `InstallUISequence`
WHERE `Action`='WixCloseApplications'
'@
    if ($null -ne $unsafeUiCloseAction) {
        throw "The standard WixCloseApplications action must remain in InstallExecuteSequence"
    }

    $legacyUiDetector = Read-Record $database @'
SELECT `Source`, `Target` FROM `CustomAction`
WHERE `Action`='CwaDetectRunningApplication'
'@
    if ($null -ne $legacyUiDetector) {
        throw "The incompatible UI process-detector custom action is still present"
    }

    $legacyUiDetectorSequence = Read-Record $database @'
SELECT `Action` FROM `InstallUISequence`
WHERE `Action`='CwaDetectRunningApplication'
'@
    if ($null -ne $legacyUiDetectorSequence) {
        throw "The incompatible UI process detector is still scheduled"
    }

    $standardCloseAction = Read-Record $database @'
SELECT `Source`, `Target` FROM `CustomAction`
WHERE `Action`='WixCloseApplications'
'@
    Assert-Equal (Read-ComField $standardCloseAction "StringData" 1) "WixCA" `
        "The process closer uses the wrong WiX binary"
    Assert-Equal (Read-ComField $standardCloseAction "StringData" 2) `
        "WixCloseApplications" `
        "The process closer calls the wrong WiX entry point"

    $standardCloseSequence = Read-Record $database @'
SELECT `Condition`, `Sequence` FROM `InstallExecuteSequence`
WHERE `Action`='WixCloseApplications'
'@
    if ($null -eq $standardCloseSequence) {
        throw "The standard process closer is missing from InstallExecuteSequence"
    }

    $closeApplication = Read-Record $database @'
SELECT `Target`, `Condition`, `Attributes`, `Property`, `TerminateExitCode`, `Timeout`
FROM `WixCloseApplication`
WHERE `WixCloseApplication`='CwaCloseRunningApplicationDuringUpgrade'
'@
    Assert-Equal (Read-ComField $closeApplication "StringData" 1) `
        "cw-assistant-desktop.exe" `
        "The application closer targets the wrong process"
    Assert-Equal (Read-ComField $closeApplication "StringData" 2) `
        "WIX_UPGRADE_DETECTED" `
        "The application closer is not confined to the execute phase"
    Assert-Equal (Read-ComField $closeApplication "StringData" 4) "" `
        "The closer must not overwrite the UI-phase detection state"
    Assert-Equal (Read-ComField $closeApplication "IntegerData" 5) 1 `
        "A stuck process does not have the expected bounded termination result"
    Assert-Equal (Read-ComField $closeApplication "IntegerData" 6) 15000 `
        "The graceful-close timeout is not 15 seconds"

    # Attribute bits: close message (1), elevated close message (4), and
    # terminate process (32). Reboot prompt (2) must remain clear.
    $closeAttributes = Read-ComField $closeApplication "IntegerData" 3
    Assert-Equal ($closeAttributes -band 37) 37 `
        "The close/terminate behavior is incomplete"
    Assert-Equal ($closeAttributes -band 2) 0 `
        "The installer must not request reboot instead of completing the update"

    $defaultAction = Read-Record $database @'
SELECT `Source`, `Target` FROM `CustomAction`
WHERE `Action`='CwaDefaultLaunchAfterClosingApplication'
'@
    Assert-Equal (Read-ComField $defaultAction "StringData" 1) `
        "WIXUI_EXITDIALOGOPTIONALCHECKBOX" `
        "The conditional default sets the wrong property"
    Assert-Equal (Read-ComField $defaultAction "StringData" 2) "1" `
        "The conditional default does not select the checkbox"

    $defaultSequence = Read-Record $database @'
SELECT `Condition`, `Sequence` FROM `InstallUISequence`
WHERE `Action`='CwaDefaultLaunchAfterClosingApplication'
'@
    Assert-Equal (Read-ComField $defaultSequence "StringData" 1) `
        'WIX_UPGRADE_DETECTED AND NOT REMOVE~="ALL"' `
        "The checkbox default is not guarded by the upgrade state"

    $executeSequence = Read-Record $database @'
SELECT `Sequence` FROM `InstallUISequence` WHERE `Action`='ExecuteAction'
'@
    if ((Read-ComField $defaultSequence "IntegerData" 2) -le
        (Read-ComField $executeSequence "IntegerData" 1)) {
        throw "The conditional checkbox default must run after installation"
    }

    $launchAction = Read-Record $database @'
SELECT `Source`, `Target` FROM `CustomAction`
WHERE `Action`='CwaLaunchApplication'
'@
    Assert-Equal (Read-ComField $launchAction "StringData" 1) "WixCA" `
        "The finish-page launch action uses the wrong WiX binary"
    Assert-Equal (Read-ComField $launchAction "StringData" 2) "WixShellExec" `
        "The finish-page launch action calls the wrong entry point"

    $launchEvent = Read-Record $database @'
SELECT `Condition` FROM `ControlEvent`
WHERE `Dialog_`='ExitDialog' AND `Control_`='Finish'
  AND `Event`='DoAction' AND `Argument`='CwaLaunchApplication'
'@
    Assert-Equal (Read-ComField $launchEvent "StringData" 1) `
        'WIXUI_EXITDIALOGOPTIONALCHECKBOX = 1 AND NOT REMOVE~="ALL"' `
        "The launch action is not guarded by the finish-page selection"
}
finally {
    if ($null -ne $database) {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    }
    if ($null -ne $installer) {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
    }
}

Write-Host "Verified Windows installer finish-page launch behavior: $($packages[0].Name)"
