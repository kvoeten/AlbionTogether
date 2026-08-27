param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v?[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$repository = Split-Path -Parent $PSScriptRoot
$releaseBuild = Join-Path $repository 'bin\Release'
$output = Join-Path $releaseBuild ("release-{0}" -f $Version)
$stage = Join-Path $output 'stage'
$archive = Join-Path $output ("AlbionTogether-{0}-win32.zip" -f $Version)

if (Test-Path -LiteralPath $output) {
    throw "Release output already exists: $output"
}

$files = [ordered]@{
    'AlbionTogether.Client.dll' = Join-Path $releaseBuild 'AlbionTogether.Client.dll'
    'AlbionTogether.Launcher.exe' = Join-Path $releaseBuild 'AlbionTogether.Launcher.exe'
    'LICENSE' = Join-Path $repository 'LICENSE'
    'README.md' = Join-Path $repository 'README.md'
    'scripts\debug\appearance_cycle.as' = Join-Path $releaseBuild 'scripts\debug\appearance_cycle.as'
}

foreach ($source in $files.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release file is missing: $source"
    }
}

New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($relativePath in $files.Keys) {
    $destination = Join-Path $stage $relativePath
    $destinationDirectory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $files[$relativePath] -Destination $destination
}

$actual = @(
    Get-ChildItem -LiteralPath $stage -Recurse -File |
        ForEach-Object {
            [System.IO.Path]::GetRelativePath($stage, $_.FullName)
        }
)
$unexpected = @($actual | Where-Object { -not $files.Contains($_) })
if ($unexpected.Count -ne 0 -or $actual.Count -ne $files.Count) {
    throw "Release staging contains files outside the allowlist: $($unexpected -join ', ')"
}

$forbiddenNames = @('game.bin', 'gamehard.bin', 'names.bin')
$forbidden = @(
    Get-ChildItem -LiteralPath $stage -Recurse -File |
        Where-Object { $_.Name.ToLowerInvariant() -in $forbiddenNames }
)
if ($forbidden.Count -ne 0) {
    throw "Retail Fable files must never be packaged: $($forbidden.FullName -join ', ')"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $stage,
    $archive,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

$zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entries = @($zip.Entries | Where-Object { -not [string]::IsNullOrEmpty($_.Name) })
    $zipForbidden = @($entries | Where-Object { $_.Name.ToLowerInvariant() -in $forbiddenNames })
    if ($entries.Count -ne $files.Count -or $zipForbidden.Count -ne 0) {
        throw 'Created archive failed the release-content verification.'
    }
}
finally {
    $zip.Dispose()
}

Write-Output $archive
