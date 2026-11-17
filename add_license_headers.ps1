# add_license_headers.ps1
$Header = @"
// MCG / CDI-AI — Copyright (C) 2026 Davidbest404
// SPDX-License-Identifier: AGPL-3.0-or-later
"@

$HtmlHeader = @"
<!-- MCG / CDI-AI — Copyright (C) 2026 Davidbest404 -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
"@

$Extensions = @("*.go", "*.js", "*.lua", "*.css")
$HtmlExtensions = @("*.html")
$Root = Get-Location

Get-ChildItem -Path $Root -Recurse -Include $Extensions | ForEach-Object {
    $content = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($content -and $content -notmatch "SPDX-License-Identifier") {
        $newContent = "$Header`r`n$content"
        Set-Content -Path $_.FullName -Value $newContent -Encoding UTF8
        Write-Host "Added header to: $($_.Name)"
    }
}

Get-ChildItem -Path $Root -Recurse -Include $HtmlExtensions | ForEach-Object {
    $content = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($content -and $content -notmatch "SPDX-License-Identifier") {
        $newContent = "$HtmlHeader`r`n$content"
        Set-Content -Path $_.FullName -Value $newContent -Encoding UTF8
        Write-Host "Added header to: $($_.Name)"
    }
}

Write-Host "All files processed successfully." -ForegroundColor Green