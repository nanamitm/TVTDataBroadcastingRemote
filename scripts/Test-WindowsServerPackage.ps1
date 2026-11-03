param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
foreach ($file in 'tvt-remote-server.exe', 'Qt6Core.dll', 'Qt6Network.dll', 'Qt6WebSockets.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll', 'qt.conf', 'LICENSE', 'THIRD_PARTY_NOTICES.md') {
    if (-not (Test-Path -LiteralPath (Join-Path $package $file))) { throw "Missing server package file: $file" }
}
if (-not (Get-ChildItem -LiteralPath (Join-Path $package 'licenses/Qt') -File)) { throw 'Qt license texts are missing' }
$savedEnvironment = @{}
foreach ($name in 'PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH') {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$stdout = [IO.Path]::GetTempFileName()
$stderr = [IO.Path]::GetTempFileName()
$server = $null
function Read-PackageLog([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH') {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $env:QT_QPA_PLATFORM = 'windows'
    $pipe = 'TVTRemote-package-' + [Guid]::NewGuid().ToString('N')
    $executable = Join-Path $package 'tvt-remote-server.exe'
    $server = Start-Process -FilePath $executable -ArgumentList '--address', '127.0.0.1', '--port', '0', '--pipe', $pipe, '--show-window' `
        -WorkingDirectory $package -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $null = $server.Handle
    $ready = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($server.HasExited) { throw "Packaged server exited before startup: $(Read-PackageLog $stderr)" }
        if ((Read-PackageLog $stdout) -match 'WS 127\.0\.0\.1:') { $ready = $true; break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $ready) { throw "Packaged server did not start: $(Read-PackageLog $stderr)" }
    $stop = Start-Process -FilePath $executable -ArgumentList '--stop', '--pipe', $pipe -WorkingDirectory $package -WindowStyle Hidden -Wait -PassThru
    if ($stop.ExitCode -ne 0) { throw 'Packaged server shutdown command failed' }
    if (-not $server.WaitForExit(5000)) { throw 'Packaged server did not exit' }
    if ($server.ExitCode -ne 0) { throw "Packaged server exited with code $($server.ExitCode)" }
    Write-Output 'Windows server package started with an isolated PATH and shut down normally.'
} finally {
    if ($server) {
        if (-not $server.HasExited) { $server.Kill(); $server.WaitForExit() }
        $server.Dispose()
    }
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
}
