param(
    [string]$Qt = 'C:\Qt\6.11.1\mingw_64',
    [string]$Compiler = 'C:\Qt\Tools\mingw1310_64\bin',
    [string]$QtSources = 'C:\Qt\6.11.1\Src',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'build/package/windows-x64' }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$env:PATH = "$Qt\bin;$Compiler;" + $env:PATH
foreach ($name in 'tvt-remote', 'tvt-remote-server', 'tvt-remote-demo', 'tvt-remote-native-demo') {
    Copy-Item -LiteralPath "$root/build/windows/$name.exe" -Destination $destination
}
& "$Qt/bin/windeployqt.exe" --release --compiler-runtime --no-translations --qmldir "$root/src/app" `
    "$destination/tvt-remote.exe" "$destination/tvt-remote-server.exe" "$destination/tvt-remote-demo.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# Enables the app's --screenshot path for headless UI checks as well.
Copy-Item -LiteralPath "$Qt/plugins/platforms/qoffscreen.dll" -Destination "$destination/platforms"
New-Item -ItemType Directory -Path "$destination/licenses/Qt", "$destination/docs" -Force | Out-Null
Copy-Item -LiteralPath "$root/README.md", "$root/LICENSE", "$root/CONTRIBUTING.md" -Destination $destination
foreach ($directory in 'docs', 'sdk', 'examples') {
    New-Item -ItemType Directory -Path "$destination/$directory" -Force | Out-Null
    Get-ChildItem -LiteralPath "$root/$directory" | Copy-Item -Destination "$destination/$directory" -Recurse -Force
}
Get-ChildItem -LiteralPath "$QtSources/qtbase/LICENSES" -File | Copy-Item -Destination "$destination/licenses/Qt"
Copy-Item -LiteralPath "$root/THIRD_PARTY_NOTICES.md" -Destination $destination
Write-Output "Windows package: $destination"
