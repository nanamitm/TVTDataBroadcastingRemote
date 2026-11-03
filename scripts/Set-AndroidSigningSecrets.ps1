param(
    [Parameter(Mandatory)][string]$SigningPropertiesPath,
    [string]$Repository = 'nanamitm/TVTDataBroadcastingRemote',
    [string]$Keytool = 'C:\Program Files\Android\Android Studio\jbr\bin\keytool.exe'
)
$ErrorActionPreference = 'Stop'
$propertiesFile = (Resolve-Path -LiteralPath $SigningPropertiesPath).Path
$signing = @{}
foreach ($line in [IO.File]::ReadAllLines($propertiesFile)) {
    if ($line -match '^\s*([^#][^=]*)=(.*)$') { $signing[$matches[1].Trim()] = $matches[2] }
}
foreach ($name in 'storeFile', 'storePassword', 'keyAlias', 'keyPassword') {
    if ([string]::IsNullOrWhiteSpace($signing[$name])) { throw "Signing property missing: $name" }
}
$store = $signing.storeFile
if (-not [IO.Path]::IsPathRooted($store)) { $store = Join-Path (Split-Path $propertiesFile) $store }
$store = (Resolve-Path -LiteralPath $store).Path
$gh = (Get-Command gh -ErrorAction Stop).Source
$certificate = [IO.Path]::GetTempFileName()
$previousPassword = $env:TVTREMOTE_KEYSTORE_STORE_PASSWORD
try {
    $env:TVTREMOTE_KEYSTORE_STORE_PASSWORD = $signing.storePassword
    & $Keytool -exportcert -keystore $store -alias $signing.keyAlias `
        -storepass:env TVTREMOTE_KEYSTORE_STORE_PASSWORD -file $certificate 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Signing certificate export failed; check the keystore and store password.' }
    $fingerprint = (Get-FileHash -LiteralPath $certificate -Algorithm SHA256).Hash.ToLowerInvariant()
    $values = @{
        ANDROID_KEYSTORE_BASE64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($store))
        ANDROID_STORE_PASSWORD = $signing.storePassword
        ANDROID_KEY_ALIAS = $signing.keyAlias
        ANDROID_KEY_PASSWORD = $signing.keyPassword
    }
    foreach ($name in $values.Keys | Sort-Object) {
        $process = [Diagnostics.Process]::new()
        try {
            $process.StartInfo.FileName = $gh
            $process.StartInfo.UseShellExecute = $false
            $process.StartInfo.CreateNoWindow = $true
            $process.StartInfo.RedirectStandardInput = $true
            $process.StartInfo.RedirectStandardError = $true
            foreach ($argument in @('secret', 'set', $name, '--repo', $Repository, '--app', 'actions')) {
                $process.StartInfo.ArgumentList.Add($argument)
            }
            [void]$process.Start()
            $process.StandardInput.Write($values[$name]); $process.StandardInput.Close()
            $null = $process.StandardError.ReadToEnd(); $process.WaitForExit()
            if ($process.ExitCode -ne 0) { throw "GitHub Secret registration failed: $name" }
        } finally { $process.Dispose() }
    }
    & $gh variable set ANDROID_SIGNER_SHA256 --repo $Repository --body $fingerprint
    if ($LASTEXITCODE -ne 0) { throw 'Signing certificate fingerprint registration failed.' }
    Write-Output "Registered Android signing Secrets and public signer fingerprint for $Repository"
} finally {
    $env:TVTREMOTE_KEYSTORE_STORE_PASSWORD = $previousPassword
    Remove-Item -LiteralPath $certificate -Force -ErrorAction SilentlyContinue
}
