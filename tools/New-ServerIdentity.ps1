param(
    [string]$Directory = (Join-Path $PSScriptRoot '../local'),
    [string]$OpenSsl = 'C:\Program Files\Git\usr\bin\openssl.exe'
)
$ErrorActionPreference = 'Stop'
$directoryPath = [IO.Path]::GetFullPath($Directory)
New-Item -ItemType Directory -Path $directoryPath -Force | Out-Null
foreach ($name in 'server-key.pem', 'server-cert.pem', 'token.txt') {
    if (Test-Path -LiteralPath (Join-Path $directoryPath $name)) { throw "Already exists: $name. Use a new directory to rotate the identity." }
}
if (-not (Test-Path -LiteralPath $OpenSsl)) { throw "OpenSSL executable not found: $OpenSsl" }
# Restrict the directory before creating any credentials; do not touch the certificate store.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetOwner($identity)
$acl.SetAccessRuleProtection($true, $false)
$rule = New-Object Security.AccessControl.FileSystemAccessRule($identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
$acl.AddAccessRule($rule)
Set-Acl -LiteralPath $directoryPath -AclObject $acl
$privateKey = Join-Path $directoryPath 'server-key.pem'
$certificate = Join-Path $directoryPath 'server-cert.pem'
& $OpenSsl req -x509 -newkey rsa:3072 -sha256 -nodes -days 365 -subj '/CN=TVTest Remote' -keyout $privateKey -out $certificate 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Certificate generation failed' }
$bytes = New-Object byte[] 32
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
$token = -join ($bytes | ForEach-Object { $_.ToString('x2') })
[IO.File]::WriteAllText((Join-Path $directoryPath 'token.txt'), $token + "`n", [Text.Encoding]::ASCII)
$fingerprint = (& $OpenSsl x509 -in $certificate -noout -fingerprint -sha256) -replace '^.*=', ''
if ($LASTEXITCODE -ne 0) { throw 'Cannot calculate certificate fingerprint' }
[IO.File]::WriteAllText((Join-Path $directoryPath 'fingerprint.txt'), $fingerprint + "`n", [Text.Encoding]::ASCII)
Write-Output "Created server identity in $directoryPath"
Write-Output "Certificate SHA-256: $fingerprint"
Write-Output 'Enter the token from token.txt into the remote app. The token and private key are not printed.'
