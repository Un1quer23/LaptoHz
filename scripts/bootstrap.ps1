param([string]$Proxy = 'http://127.0.0.1:7890')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot '.tools'
New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null

$packages = @(
    @{ Repo='mstorsjo/llvm-mingw'; Tag='20260922'; Asset='llvm-mingw-20260922-ucrt-x86_64.zip'; Directory='llvm-mingw-20260922-ucrt-x86_64'; Check='bin\clang++.exe' },
    @{ Repo='Kitware/CMake'; Tag='v4.4.4'; Asset='cmake-4.4.4-windows-x86_64.zip'; Directory='cmake-4.4.4-windows-x86_64'; Check='bin\cmake.exe' },
    @{ Repo='ninja-build/ninja'; Tag='v1.13.2'; Asset='ninja-win.zip'; Directory='ninja'; Check='ninja.exe' }
)
foreach ($package in $packages) {
    $destination = Join-Path $toolsRoot $package.Directory
    if (Test-Path -LiteralPath (Join-Path $destination $package.Check)) { continue }
    $archive = Join-Path $toolsRoot $package.Asset
    $url = 'https://github.com/{0}/releases/download/{1}/{2}' -f $package.Repo,$package.Tag,$package.Asset
    Write-Output ('Downloading {0}' -f $package.Asset)
    Invoke-WebRequest -Uri $url -OutFile $archive -Proxy $Proxy
    $assetPage = Invoke-WebRequest -Uri ('https://github.com/{0}/releases/expanded_assets/{1}' -f $package.Repo,$package.Tag) -Proxy $Proxy
    $assetBlock = [regex]::Matches($assetPage.Content, '<li\b[^>]*>.*?</li>', [Text.RegularExpressions.RegexOptions]::Singleline) | Where-Object { $_.Value.Contains($package.Asset) } | Select-Object -First 1
    $digestMatch = if ($assetBlock) { [regex]::Match($assetBlock.Value, 'sha256:([0-9a-f]{64})') } else { $null }
    $actualDigest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($digestMatch -and $digestMatch.Success) {
        if ($actualDigest -ne $digestMatch.Groups[1].Value) { throw ('Checksum mismatch: {0}' -f $package.Asset) }
        Write-Output 'Publisher SHA256 verified.'
    } elseif ($package.Repo -eq 'Kitware/CMake') {
        $checksums = Invoke-WebRequest -Uri 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-SHA-256.txt' -Proxy $Proxy
        $checksumLine = $checksums.Content -split "`n" | Where-Object { $_.EndsWith($package.Asset) } | Select-Object -First 1
        if (-not $checksumLine -or $actualDigest -ne ($checksumLine -split '\s+')[0]) { throw 'CMake checksum mismatch.' }
        Write-Output 'Publisher SHA256 verified.'
    } else { throw ('Publisher SHA256 unavailable: {0}' -f $package.Asset) }
    if ($package.Directory -eq 'ninja') { Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force }
    else { Expand-Archive -LiteralPath $archive -DestinationPath $toolsRoot -Force }
    Write-Output ('Ready: {0}' -f $package.Directory)
}
