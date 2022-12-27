param (
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",

    [Parameter(Mandatory = $false)]
    [ValidateSet("x86", "x64", "arm", "arm64", "arm64ec")]
    [string]$Arch = "x64",

    [Parameter(Mandatory = $false)]
    [ValidateSet("schannel", "openssl")]
    [string]$Tls = "openssl",

    [Parameter(Mandatory = $false)]
    [ValidateSet("static", "shared")]
    [string]$Link = "static",

    [Parameter(Mandatory = $false)]
    [ValidateSet("client-only", "server")]
    [string]$Mode = "server",

    [Parameter(Mandatory = $false)]
    [string]$BuildId = "0",

    [Parameter(Mandatory = $false)]
    [string]$Suffix = "-private",

    [Parameter(Mandatory = $false)]
    [switch]$Clean = $false,

    [Parameter(Mandatory = $false)]
    [switch]$WithTools = $false,

    [Parameter(Mandatory = $false)]
    [switch]$WithTests = $false
)

Set-StrictMode -Version 'Latest'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

if ($Clean) {
    if (Test-Path "./build") { Remove-Item "./build" -Recurse -Force | Out-Null }
    if (Test-Path "./artifacts") { Remove-Item "./artifacts" -Recurse -Force | Out-Null }
}

if (!(Test-Path "./build")) {
    New-Item -Path "./build" -ItemType Directory -Force | Out-Null
}

if (!(Test-Path "./artifacts")) {
    New-Item -Path "./artifacts" -ItemType Directory -Force | Out-Null
}

$Build = Resolve-Path ./build
$Artifacts = Resolve-Path ./artifacts

$Shared = "off"
if ($Link -ne "static") { $Shared = "on" }

$Server = "off"
if ($Mode -eq "server") { $Server = "on" }

$Tools = "off"
if ($WithTools) { $Tools = "on" }

$Tests = "off"
if ($WithTests) { $Tests = "on" }

function Execute([String]$Name, [String]$Arguments) {
    Write-Debug "$Name $Arguments"
    $process = Start-Process $Name $Arguments -PassThru -NoNewWindow -WorkingDirectory $Build
    $handle = $process.Handle # Magic work around. Don't remove this line.
    $process.WaitForExit();
    if ($process.ExitCode -ne 0) {
        Write-Error "$Name exited with status code $($process.ExitCode)"
    }
}

if ($IsWindows) {

    $_Arch = $Arch
    if ($_Arch -eq "x86") { $_Arch = "Win32" }
    Execute "cmake" "-G ""Visual Studio 17 2022"" -A $_Arch -DMSH3_OUTPUT_DIR=$Artifacts -DQUIC_TLS=$Tls -DQUIC_BUILD_SHARED=$Shared -DMSH3_SERVER_SUPPORT=$Server -DMSH3_TEST=$Tests -DMSH3_TOOL=$Tools -DMSH3_VER_BUILD_ID=$BuildId -DMSH3_VER_SUFFIX=$Suffix .."
    Execute "cmake" "--build . --config $Config"

} else {

    $BuildType = $Config
    if ($BuildType -eq "Release") { $BuildType = "RelWithDebInfo" }
    Execute "cmake" "-G ""Unix Makefiles"" -DCMAKE_BUILD_TYPE=$BuildType -DMSH3_OUTPUT_DIR=$Artifacts -DQUIC_TLS=$Tls -DQUIC_BUILD_SHARED=$Shared -DMSH3_SERVER_SUPPORT=$Server -DMSH3_TEST=$Tests -DMSH3_TOOL=$Tools .."
    Execute "cmake" "--build ."
}
