param([switch]$Portable, [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($Portable) {
    $llvmBin = Join-Path $projectRoot '.tools\llvm-mingw-20260922-ucrt-x86_64\bin'
    $cmakeBin = Join-Path $projectRoot '.tools\cmake-4.4.4-windows-x86_64\bin'
    $ninjaBin = Join-Path $projectRoot '.tools\ninja'
    if (-not (Test-Path -LiteralPath (Join-Path $llvmBin 'clang++.exe'))) { throw 'Run scripts/bootstrap.ps1 first.' }
    $env:PATH = $llvmBin + ';' + $cmakeBin + ';' + $ninjaBin + ';' + $env:PATH
    $cmake = Join-Path $cmakeBin 'cmake.exe'
    $buildDirectory = Join-Path $projectRoot ('build\portable-' + $Configuration.ToLowerInvariant())
    & $cmake -S $projectRoot -B $buildDirectory -G Ninja '-DCMAKE_CXX_COMPILER=clang++.exe' '-DCMAKE_RC_COMPILER=llvm-windres.exe' ('-DCMAKE_BUILD_TYPE=' + $Configuration)
} else {
    $cmake = 'cmake'
    $buildDirectory = Join-Path $projectRoot 'build\msvc'
    & $cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
}
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $buildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& (Join-Path (Split-Path (Get-Command $cmake).Source) 'ctest.exe') --test-dir $buildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
if ($Configuration -eq 'Release') {
    & $cmake --install $buildDirectory --config Release --prefix (Join-Path $projectRoot 'dist\LaptoHz')
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
    $packageDirectory = Join-Path $projectRoot 'dist\LaptoHz'
    $checksums = Get-ChildItem -LiteralPath $packageDirectory -Recurse -File |
        Where-Object Name -NE 'SHA256SUMS.txt' | Sort-Object FullName | ForEach-Object {
            $relative = $_.FullName.Substring($packageDirectory.Length+1).Replace('\','/')
            (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash + '  ' + $relative
        }
    $checksums | Set-Content -LiteralPath (Join-Path $packageDirectory 'SHA256SUMS.txt') -Encoding ascii
}
