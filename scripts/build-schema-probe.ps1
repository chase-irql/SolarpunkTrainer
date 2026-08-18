[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [ValidateSet('x64')]
    [string] $Platform = 'x64',

    [Parameter(Mandatory = $true)]
    [string] $OutDir,

    [Parameter(Mandatory = $true)]
    [string] $MSBuildPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$dependencyParent = Join-Path $repositoryRoot '.deps'
$dependencyRoot = Join-Path $dependencyParent 'Dumper-7'
$upstreamUrl = 'https://github.com/Encryqed/Dumper-7.git'
$upstreamCommit = '3a849bb838422bea5cf417447d00a99549d932cf'
$patchPath = Join-Path $repositoryRoot 'schema_probe\dumper-7.patch'
$overlayRoot = Join-Path $repositoryRoot 'schema_probe\overlay\Dumper'
$markerPath = Join-Path $dependencyRoot '.solarpunk-schema-probe-recipe'

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,

        [Parameter(Mandatory = $true)]
        [string[]] $ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE."
    }
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string] $Path)

    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = $algorithm.ComputeHash($stream)
        return ([BitConverter]::ToString($digest) -replace '-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw "MSBuild was not found at '$MSBuildPath'."
}

foreach ($requiredFile in @(
    $patchPath,
    (Join-Path $overlayRoot 'RuntimeSchema.cpp'),
    (Join-Path $overlayRoot 'RuntimeSchema.h')
)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required schema-probe source is missing: $requiredFile"
    }
}

$recipe = @(
    $upstreamCommit
    (Get-FileSha256 -Path $patchPath)
    (Get-FileSha256 -Path (Join-Path $overlayRoot 'RuntimeSchema.cpp'))
    (Get-FileSha256 -Path (Join-Path $overlayRoot 'RuntimeSchema.h'))
) -join "`n"

if (-not (Test-Path -LiteralPath (Join-Path $dependencyRoot '.git'))) {
    if (Test-Path -LiteralPath $dependencyRoot) {
        throw "'$dependencyRoot' exists but is not a prepared Git checkout. Remove it and rebuild."
    }

    New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
    Invoke-Checked -FilePath 'git' -ArgumentList @('-C', $dependencyRoot, 'init')
    Invoke-Checked -FilePath 'git' -ArgumentList @(
        '-C', $dependencyRoot, 'remote', 'add', 'origin', $upstreamUrl
    )
    Invoke-Checked -FilePath 'git' -ArgumentList @(
        '-C', $dependencyRoot, 'fetch', '--depth', '1', 'origin', $upstreamCommit
    )
    Invoke-Checked -FilePath 'git' -ArgumentList @(
        '-C', $dependencyRoot, 'checkout', '--detach', 'FETCH_HEAD'
    )
}

$actualRemote = (& git -C $dependencyRoot remote get-url origin).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRemote -ne $upstreamUrl) {
    throw "The cached Dumper-7 checkout has an unexpected origin. Remove '$dependencyRoot' and rebuild."
}

$actualCommit = (& git -C $dependencyRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $upstreamCommit) {
    throw "The cached Dumper-7 checkout is not at the pinned revision. Remove '$dependencyRoot' and rebuild."
}

$prepared = Test-Path -LiteralPath $markerPath -PathType Leaf
if ($prepared) {
    $actualRecipe = (Get-Content -LiteralPath $markerPath -Raw).TrimEnd()
    if ($actualRecipe -ne $recipe) {
        throw "The schema-probe recipe changed. Remove '$dependencyRoot' and rebuild."
    }

    foreach ($overlayName in @('RuntimeSchema.cpp', 'RuntimeSchema.h')) {
        $sourceHash = Get-FileSha256 -Path (Join-Path $overlayRoot $overlayName)
        $targetPath = Join-Path (Join-Path $dependencyRoot 'Dumper') $overlayName
        if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf) -or
            (Get-FileSha256 -Path $targetPath) -ne $sourceHash) {
            throw "The cached schema-probe source was modified. Remove '$dependencyRoot' and rebuild."
        }
    }
} else {
    $workingChanges = (& git -C $dependencyRoot status --porcelain)
    if ($LASTEXITCODE -ne 0) {
        throw "The cached Dumper-7 checkout could not be inspected."
    }

    if ($workingChanges) {
        & git -C $dependencyRoot apply --reverse --check $patchPath 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "The cached Dumper-7 checkout has unexpected changes. Remove '$dependencyRoot' and rebuild."
        }

        foreach ($overlayName in @('RuntimeSchema.cpp', 'RuntimeSchema.h')) {
            $sourceHash = Get-FileSha256 -Path (Join-Path $overlayRoot $overlayName)
            $targetPath = Join-Path (Join-Path $dependencyRoot 'Dumper') $overlayName
            if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf) -or
                (Get-FileSha256 -Path $targetPath) -ne $sourceHash) {
                throw "The partially prepared schema-probe source is invalid. Remove '$dependencyRoot' and rebuild."
            }
        }
    } else {
        Invoke-Checked -FilePath 'git' -ArgumentList @(
            '-C', $dependencyRoot, 'apply', '--check', $patchPath
        )
        Invoke-Checked -FilePath 'git' -ArgumentList @(
            '-C', $dependencyRoot, 'apply', $patchPath
        )

        Copy-Item -LiteralPath (Join-Path $overlayRoot 'RuntimeSchema.cpp') `
            -Destination (Join-Path $dependencyRoot 'Dumper\RuntimeSchema.cpp')
        Copy-Item -LiteralPath (Join-Path $overlayRoot 'RuntimeSchema.h') `
            -Destination (Join-Path $dependencyRoot 'Dumper\RuntimeSchema.h')
    }

    $utf8NoBom = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText(
        $markerPath,
        $recipe + [Environment]::NewLine,
        $utf8NoBom)
}

$resolvedOutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $resolvedOutDir -Force | Out-Null
$outDirWithSeparator = $resolvedOutDir.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$intermediateDir = Join-Path $dependencyRoot "x64\$Configuration\SolarpunkSchemaProbe"
$intermediateWithSeparator = $intermediateDir.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$probeProject = Join-Path $dependencyRoot 'Dumper\Dumper.vcxproj'

Invoke-Checked -FilePath $MSBuildPath -ArgumentList @(
    $probeProject,
    '/m',
    '/nr:false',
    '/v:minimal',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:SchemaProbe=true',
    "/p:OutDir=$outDirWithSeparator",
    "/p:IntDir=$intermediateWithSeparator"
)

$probeOutput = Join-Path $resolvedOutDir 'SolarpunkSchemaProbe.dll'
if (-not (Test-Path -LiteralPath $probeOutput -PathType Leaf)) {
    throw "The schema probe build completed without producing '$probeOutput'."
}

Write-Host "Schema probe ready: $probeOutput"
