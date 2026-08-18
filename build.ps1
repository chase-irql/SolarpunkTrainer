[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer could not be found. Install Visual Studio 2022 with Desktop development with C++.'
}

$msbuild = & $vswhere `
    -latest `
    -products '*' `
    -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' |
    Select-Object -First 1
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw 'MSBuild could not be found. Install Visual Studio 2022 with Desktop development with C++.'
}

Invoke-Checked -FilePath 'git' -ArgumentList @(
    '-C', $PSScriptRoot, 'submodule', 'update', '--init', '--recursive'
)

Invoke-Checked -FilePath $msbuild -ArgumentList @(
    (Join-Path $PSScriptRoot 'SolarpunkTrainer.sln'),
    '/m',
    '/nr:false',
    '/v:minimal',
    "/p:Configuration=$Configuration",
    '/p:Platform=x64'
)

Write-Host "Build complete: $(Join-Path $PSScriptRoot "x64\$Configuration")"
