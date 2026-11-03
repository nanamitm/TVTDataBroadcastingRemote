param(
    [string]$Qt = 'C:\Qt\6.11.1\android_arm64_v8a',
    [string]$HostQt = 'C:\Qt\6.11.1\mingw_64',
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$NdkVersion = '27.2.12479018',
    [string]$Java = 'C:\Program Files\Android\Android Studio\jbr',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'C:\Qt\Tools\Ninja',
    [string]$BuildDirectory = 'build/android-arm64-lan'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $root $BuildDirectory
$env:JAVA_HOME = $Java
$env:PATH = "$Ninja;" + $env:PATH
$ndk = Join-Path $Sdk "ndk/$NdkVersion"
& $CMake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_TOOLCHAIN_FILE=$Qt/lib/cmake/Qt6/qt.toolchain.cmake" "-DQT_HOST_PATH=$HostQt" `
    "-DANDROID_SDK_ROOT=$Sdk" "-DANDROID_NDK_ROOT=$ndk" "-DCMAKE_ANDROID_NDK=$ndk" -DREMOTE_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw 'Android configuration failed' }
& $CMake --build $build --target apk --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Android build failed' }
# Package a locally installable development APK with the standard Android debug
# signing identity. No release signing key or password is required or accessed.
& "$HostQt/bin/androiddeployqt.exe" --input "$build/android-tvt-remote-deployment-settings.json" `
    --output "$build/android-build" --android-platform android-36 --jdk $Java --gradle
if ($LASTEXITCODE -ne 0) { throw 'Development APK packaging failed' }
Write-Output "Development APK: $build/android-build/build/outputs/apk/debug/android-build-debug.apk"
