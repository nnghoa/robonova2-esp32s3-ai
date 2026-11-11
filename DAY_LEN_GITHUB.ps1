param(
    [string]$RepoUrl = ""
)

Set-Location $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($RepoUrl)) {
    Write-Host "==========================================================" -ForegroundColor Cyan
    Write-Host "  DAY FIRMWARE DB-ROBONOVA2 ESP32-S3 AI LEN GITHUB" -ForegroundColor Cyan
    Write-Host "==========================================================" -ForegroundColor Cyan
    Write-Host "1. Mo https://github.com/new va tao 1 repository TRONG (VD: robonova2-esp32s3-ai)"
    Write-Host "   (Khong tich chon Add a README file)"
    $RepoUrl = Read-Host "2. Dan link HTTPS cua repository vao day (VD: https://github.com/ten_anh/robonova2-esp32s3-ai.git)"
}

if ([string]::IsNullOrWhiteSpace($RepoUrl)) {
    Write-Host "Chua nhap link GitHub repository!" -ForegroundColor Red
    exit 1
}

git remote set-url origin $RepoUrl
Write-Host "`nDang day code len GitHub (Trinh duyet se mo cua so dang nhap GitHub neu chua dang nhap)..." -ForegroundColor Yellow
git push -u origin robonova2-vn:main --force

if ($LASTEXITCODE -eq 0) {
    $cleanUrl = $RepoUrl -replace '\.git$', ''
    Write-Host "`n==========================================================" -ForegroundColor Green
    Write-Host "THANH CONG! GitHub Actions dang tu dong bien dich file .bin!" -ForegroundColor Green
    Write-Host "- Xem tien trinh build (khoang 4-5 phut): $cleanUrl/actions" -ForegroundColor Cyan
    Write-Host "- Tai thang file firmware_db_robonova2_s3_0x0.bin tai: $cleanUrl/releases" -ForegroundColor Cyan
    Write-Host "==========================================================" -ForegroundColor Green
} else {
    Write-Host "`nCo loi khi push. Vui long kiem tra lai link repo hoac dang nhap GitHub!" -ForegroundColor Red
}
