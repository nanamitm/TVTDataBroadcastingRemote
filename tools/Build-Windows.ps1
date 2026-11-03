param(
    [string]$Qt = 'C:\Qt\6.11.1\mingw_64',
    [string]$Compiler = 'C:\Qt\Tools\mingw1310_64\bin',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'C:\Qt\Tools\Ninja'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $root 'build/windows'
$env:PATH = "$Qt\bin;$Compiler;$Ninja;" + $env:PATH
& $CMake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
& $CMake --build $build --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed' }
& (Join-Path (Split-Path $CMake) 'ctest.exe') --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed; see $build/test-results.txt" }
