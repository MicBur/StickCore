$ErrorActionPreference = "Stop"

Write-Host "=== StickCore: Assembling Local Release Package ===" -ForegroundColor Cyan

$deployDir = "e:\StickCore\deploy"
$outputDir = "e:\StickCore\Output"

if (!(Test-Path $deployDir)) { New-Item -ItemType Directory -Path $deployDir | Out-Null }
if (!(Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir | Out-Null }

# 1. Update StickCore.exe from build-local
Copy-Item "e:\StickCore\build-local\StickCore.exe" "$deployDir\StickCore.exe" -Force
Write-Host "✓ Copied latest StickCore.exe"

# 2. Run windeployqt
Write-Host "Running windeployqt..."
& "C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe" --release --compiler-runtime "$deployDir\StickCore.exe"
Write-Host "✓ windeployqt completed"

# 3. Create fonts directory and copy all fonts
$fontsDir = "$deployDir\fonts"
if (!(Test-Path $fontsDir)) { New-Item -ItemType Directory -Path $fontsDir | Out-Null }

Copy-Item "e:\StickCore\assets\fonts\*.ttf" $fontsDir -Force
Copy-Item "e:\StickCore\media\fonts\*.ttf" $fontsDir -Force
$fontCount = (Get-ChildItem $fontsDir -Filter "*.ttf").Count
Write-Host "✓ Copied $fontCount TrueType fonts into deploy\fonts"

# 4. Copy logo and icon assets
Copy-Item "e:\StickCore\media\stickcore.ico" "$deployDir\stickcore.ico" -Force
Copy-Item "e:\StickCore\media\stickcore_logo.png" "$deployDir\stickcore_logo.png" -Force
Copy-Item "e:\StickCore\media\stickcore_badge.png" "$deployDir\stickcore_badge.png" -Force
Write-Host "✓ Copied branding & icon assets"

# 5. Create 1-Click Font Installer script in deploy
$fontInstallerScript = @'
# 1-Click Font Installer for Windows
$fonts = Get-ChildItem -Path "$PSScriptRoot\fonts" -Filter "*.ttf"
$fontReg = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts"
$destDir = "$env:WINDIR\Fonts"

Write-Host "Installiere $($fonts.Count) StickCore-Schriften in Windows..." -ForegroundColor Cyan
foreach ($f in $fonts) {
    $target = Join-Path $destDir $f.Name
    if (!(Test-Path $target)) {
        Copy-Item $f.FullName $target -Force
        $fontName = [System.IO.Path]::GetFileNameWithoutExtension($f.Name) + " (TrueType)"
        New-ItemProperty -Path $fontReg -Name $fontName -Value $f.Name -PropertyType String -Force | Out-Null
    }
}
Write-Host "Alle Schriften erfolgreich installiert!" -ForegroundColor Green
'@
Set-Content -Path "$deployDir\Install-Fonts.ps1" -Value $fontInstallerScript -Encoding UTF8

# 6. Create 1-Click Desktop App Setup script for Daniela
$setupBat = @'
@echo off
chcp 65001 >nul
echo ===================================================
echo     StickCore Studio v1.0.0 - Setup & Installation
echo ===================================================
echo.

set "TARGET_DIR=%LOCALAPPDATA%\Programs\StickCore"
echo [1/3] Kopiere Programmdateien nach %TARGET_DIR%...
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
xcopy /E /I /Y "%~dp0*" "%TARGET_DIR%\" >nul

echo [2/3] Registriere Desktop- & Startmenue-Verknuepfungen...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([Environment]::GetFolderPath('Desktop') + '\StickCore Studio.lnk'); $s.TargetPath = '%TARGET_DIR%\StickCore.exe'; $s.WorkingDirectory = '%TARGET_DIR%'; $s.IconLocation = '%TARGET_DIR%\stickcore.ico,0'; $s.Description = 'StickCore Studio — Professionelle Sticksoftware'; $s.Save()"

powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $smDir = [Environment]::GetFolderPath('Programs') + '\StickCore'; if (!(Test-Path $smDir)) { New-Item -ItemType Directory -Path $smDir | Out-Null }; $s = $ws.CreateShortcut($smDir + '\StickCore Studio.lnk'); $s.TargetPath = '%TARGET_DIR%\StickCore.exe'; $s.WorkingDirectory = '%TARGET_DIR%'; $s.IconLocation = '%TARGET_DIR%\stickcore.ico,0'; $s.Description = 'StickCore Studio'; $s.Save()"

echo [3/3] Registriere alle Schriftarten (Varsity, Graduate, Jersey, etc.)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%TARGET_DIR%\Install-Fonts.ps1" 2>nul

echo.
echo ===================================================
echo    Installation abgeschlossen!
echo    Sie koennen StickCore jetzt vom Desktop starten.
echo ===================================================
echo.
pause
start "" "%TARGET_DIR%\StickCore.exe"
'@
Set-Content -Path "$deployDir\Setup-StickCore.bat" -Value $setupBat -Encoding OEM

# Also create portable launcher
$runBat = @'
@echo off
start "" "%~dp0StickCore.exe" %*
'@
Set-Content -Path "$deployDir\StickCore.bat" -Value $runBat -Encoding OEM

# 7. Create ZIP package
$zipFile = "$outputDir\StickCore-Portable-v1.0.0.zip"
if (Test-Path $zipFile) { Remove-Item $zipFile -Force }
Write-Host "Creating ZIP archive: $zipFile..."
Compress-Archive -Path "$deployDir\*" -DestinationPath $zipFile -Force
$zipSizeMb = [math]::Round((Get-Item $zipFile).Length / 1MB, 2)
Write-Host "Created ZIP package: $zipFile ($zipSizeMb MB)" -ForegroundColor Green

Write-Host "=== Package Assembly Complete! ===" -ForegroundColor Cyan
