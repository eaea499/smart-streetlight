param(
    [string]$ServerPort = "8080"
)

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

function Find-Maven {
    $mvn = Get-Command mvn -ErrorAction SilentlyContinue
    if ($mvn) {
        return $mvn.Source
    }

    $candidates = Get-ChildItem -Path "$env:USERPROFILE\.m2\wrapper\dists" -Recurse -Filter mvn.cmd -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending

    if ($candidates.Count -gt 0) {
        return $candidates[0].FullName
    }

    return $null
}

$maven = Find-Maven
if (-not $maven) {
    Write-Error "Maven was not found. Install Maven, add mvn to PATH, or run the project from IntelliJ IDEA."
}

Write-Host "Using Maven: $maven"

& $maven spring-boot:run "-Dspring-boot.run.arguments=--server.port=$ServerPort"
