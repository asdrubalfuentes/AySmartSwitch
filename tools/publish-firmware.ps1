param(
    [string]$EnvName,           # PlatformIO env, e.g. esp01_1m or esp32dev
    [string]$FtpHost,           # e.g. ftp.aysafi.com
    [string]$FtpUser,
    [string]$FtpPass,
    [string]$RemoteDir,         # remote dir path, e.g. /public_html/AySmartSwitch/
    [ValidateSet('php','txt')] [string]$VersionFormat = 'php',
    [ValidateSet('FTP','FTPS')] [string]$Protocol = 'FTPS',
    [string]$VersionFilePath,
    [switch]$UsePassive,
    [string]$Version,           # Optional override for version (e.g., 01.02.125)
    [switch]$StableOnly         # If set, only upload as firmware.bin (no versioned filename)
)

# Load .env/.env.local if present for defaults
$ErrorActionPreference = 'Stop'
$EnvMap = @{}
function Load-DotEnv {
    param([string]$path)
    if (-not (Test-Path $path)) { return }
    Get-Content -Path $path -Encoding UTF8 | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith('#') -and $line.Contains('=')) {
            $k,$v = $line.Split('=',2)
            $EnvMap[$k.Trim()] = $v.Trim()
        }
    }
}

if (Test-Path ".env.local") { Load-DotEnv -path ".env.local" }
elseif (Test-Path ".env") { Load-DotEnv -path ".env" }

function _val($current, $key, $default=$null) {
    if ($null -ne $current -and $current -ne '') { return $current }
    if ($EnvMap.ContainsKey($key)) { return $EnvMap[$key] }
    return $default
}

$EnvName   = _val $EnvName   'ENV_NAME'   'esp01_1m'
$FtpHost   = _val $FtpHost   'FTP_HOST'
$FtpUser   = _val $FtpUser   'FTP_USER'
$FtpPass   = _val $FtpPass   'FTP_PASS'
$RemoteDir = _val $RemoteDir 'FTP_REMOTE_DIR' '/public_html/AySmartSwitch/'
$Protocol  = _val $Protocol  'FTP_PROTOCOL' 'FTPS'
$VersionFilePath = _val $VersionFilePath 'VERSION_FILE_PATH'
$passiveStr = _val ($UsePassive.IsPresent) 'FTP_PASSIVE'
if ($passiveStr -is [string]) { $UsePassive = [bool]::Parse($passiveStr) }

if (-not $FtpHost -or -not $FtpUser -or -not $FtpPass) {
    throw "FTP configuration missing. Provide params or .env(.local) with FTP_HOST/FTP_USER/FTP_PASS."
}
if (-not $RemoteDir.EndsWith('/')) { $RemoteDir = $RemoteDir + '/' }

# This script uploads the built firmware binary and a simple version file (index.php or version.txt)
# using FTP. It expects PlatformIO to have produced a file named like:
#   .pio/build/<EnvName>/<EnvName>-<FW_VERSION_SHORT>.bin
# Example: .pio/build/esp01_1m/esp01_1m-01.02.125.bin



function Find-Firmware {
    param([string]$envName)
    $dir = Join-Path ".pio/build" $envName
    if (-not (Test-Path $dir)) { throw "Build output not found: $dir" }
    # Prefer default name firmware.bin; fallback to envName-*.bin
    $fwDefault = Join-Path $dir "firmware.bin"
    if (Test-Path $fwDefault) { return Get-Item $fwDefault }
    $bins = Get-ChildItem -Path $dir -Filter "$envName-*.bin" | Sort-Object LastWriteTime -Descending
    if (-not $bins -or $bins.Count -eq 0) { throw "No firmware bin found in $dir" }
    return $bins[0]
}

function Parse-VersionFromName {
    param([string]$name)
    # name pattern: <env>-<version>.bin  => extract <version>
    if ($name -match "^[^-]+-(?<ver>[^.]+\.[^.]+\.[^.]+)\.bin$") {
        return $Matches['ver']
    }
    # fallback: try last two dots part
    $base = [IO.Path]::GetFileNameWithoutExtension($name)
    $idx = $base.IndexOf('-')
    if ($idx -gt 0) { return $base.Substring($idx+1) }
    throw "Cannot parse version from $name"
}

function New-VersionFile {
    param([string]$version, [string]$format)
    $tmp = New-Item -ItemType Directory -Force -Path (Join-Path $env:TEMP "fwpub") | Select-Object -ExpandProperty FullName
    if ($format -eq 'php') {
        $path = Join-Path $tmp "index.php"
        @("<?php", "header('Content-Type: text/plain');", "echo '" + $version + "';") | Set-Content -Encoding UTF8 -Path $path
    } else {
        $path = Join-Path $tmp "version.txt"
        Set-Content -Encoding UTF8 -Path $path -Value $version
    }
    return $path
}

function Upload-FTP {
    param([string]$localPath, [string]$remoteName)
    $uri = "ftp://$FtpHost$RemoteDir$remoteName"
    $bytes = [System.IO.File]::ReadAllBytes($localPath)
    $req = [System.Net.FtpWebRequest]::Create($uri)
    $req.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
    $req.Credentials = New-Object System.Net.NetworkCredential($FtpUser, $FtpPass)
    $req.UseBinary = $true
    $req.KeepAlive = $false
    $req.UsePassive = [bool]$UsePassive
    $req.EnableSsl = ($Protocol -eq 'FTPS')
    Write-Host "Uploading $localPath -> $uri ($Protocol)"
    $req.ContentLength = $bytes.Length
    $stream = $req.GetRequestStream()
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Close()
    $resp = $req.GetResponse()
    $resp.Close()
}

# 1) Locate firmware
$fw = Find-Firmware -envName $EnvName
$fwFull = $fw.FullName
$fwName = $fw.Name

# Resolve version
if ($Version -and $Version.Trim() -ne '') {
    $version = $Version.Trim()
} else {
    # Best-effort parse from filename when possible
    try { $version = Parse-VersionFromName -name $fwName } catch { $version = $null }
    if (-not $version) { throw "Version not provided and cannot be inferred from filename '$fwName'" }
}

# 2) Select or create version file (if Version provided, generate one to ensure content)
if ($Version -and $Version.Trim() -ne '') {
    $versionFile = New-VersionFile -version $version -format $VersionFormat
} elseif ($VersionFilePath) {
    $versionFile = (Resolve-Path $VersionFilePath).Path
} elseif (Test-Path "index.php") {
    $versionFile = (Resolve-Path "index.php").Path
} else {
    $versionFile = New-VersionFile -version $version -format $VersionFormat
}

# 3) Upload firmware
if ($StableOnly) {
    Upload-FTP -localPath $fwFull -remoteName "firmware.bin"
} else {
    Upload-FTP -localPath $fwFull -remoteName $fwName
    Upload-FTP -localPath $fwFull -remoteName "firmware.bin"
}

# 4) Upload version indicator (index.php or version.txt)
$remoteVerName = (Split-Path -Leaf $versionFile)
Upload-FTP -localPath $versionFile -remoteName $remoteVerName

Write-Host "Publish complete: version $version"
