# build.ps1 - Build MCG for Windows
$ErrorActionPreference = "Stop"

Write-Host "1. Copying client to server static..." -ForegroundColor Cyan
if (Test-Path "server\web\static") { Remove-Item -Recurse -Force "server\web\static" }
New-Item -ItemType Directory -Force -Path "server\web\static" | Out-Null
Copy-Item -Recurse -Force "client\*" "server\web\static\"

Write-Host "2. Building Go server..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path "dist" | Out-Null
$env:CGO_ENABLED = "0"
$env:GOOS = "windows"
$env:GOARCH = "amd64"

go build -o "dist\mcg-server.exe" ./server

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED! Check the errors above." -ForegroundColor Red
    exit 1
}

Write-Host "Build finished successfully! File: dist\mcg-server.exe" -ForegroundColor Green