param(
    [Parameter(Mandatory=$true)] [string]$EnvName,
    [Parameter(Mandatory=$true)] [string]$Url,          # e.g. https://aysafi.com/AySmartSwitch/index.php
    [Parameter(Mandatory=$true)] [string]$Version,      # e.g. 01.02.125
    [string]$ApiKey
)

$ErrorActionPreference = 'Stop'

"use strict"
# Ensure TLS 1.2+
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

# Locate firmware.bin
$buildDir = Join-Path ".pio/build" $EnvName
$firmwarePath = Join-Path $buildDir "firmware.bin"
if (-not (Test-Path $firmwarePath)) {
    throw "firmware.bin not found at $firmwarePath"
}

# Prepare multipart form
$form = @{
    firmware_version = $Version
    file = Get-Item -LiteralPath $firmwarePath
}

$headers = @{
    'User-Agent' = 'curl/7.79.1'
    'Accept' = 'application/json, text/plain, */*'
}
if ($ApiKey -and $ApiKey.Trim() -ne '') { $headers['X-Api-Key'] = $ApiKey }

Write-Host "POST $Url (version=$Version, file=$firmwarePath)"
try {
    $resp = Invoke-RestMethod -Uri $Url -Method Post -Form $form -Headers $headers -TimeoutSec 120
    if ($resp -and $resp.ok -eq $true) {
        Write-Host "HTTP publish OK (version $($resp.version))"
    } else {
        Write-Host ($resp | ConvertTo-Json -Depth 5)
        throw "HTTP publish returned unexpected response"
    }
} catch {
    Write-Host "HTTP publish error: $($_.Exception.Message)"
    if ($_.Exception.Response -and $_.Exception.Response.Content) {
        try { Write-Host ( $_.Exception.Response.Content | Out-String ) } catch {}
    }
    throw
}
