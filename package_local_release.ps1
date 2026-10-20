$ErrorActionPreference = "Stop"

Write-Host "=== StickCore: Assembling Local Release Package & Installer ===" -ForegroundColor Cyan

$deployDir = "E:\StickCore\deploy"
$outputDir = "E:\StickCore\output"

if (!(Test-Path $deployDir)) { New-Item -ItemType Directory -Path $deployDir | Out-Null }
if (!(Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir | Out-Null }

# 1. Update StickCore.exe from build-local
Copy-Item "E:\StickCore\build-local\StickCore.exe" "$deployDir\StickCore.exe" -Force
Write-Host "[OK] Copied latest StickCore.exe"

# 2. Run windeployqt
Write-Host "Running windeployqt..."
& "C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe" --release --compiler-runtime "$deployDir\StickCore.exe"
Write-Host "[OK] windeployqt completed"

# 3. Create fonts directory and copy all fonts
$fontsDir = "$deployDir\fonts"
if (!(Test-Path $fontsDir)) { New-Item -ItemType Directory -Path $fontsDir | Out-Null }

Copy-Item "E:\StickCore\assets\fonts\*.ttf" $fontsDir -Force
Copy-Item "E:\StickCore\media\fonts\*.ttf" $fontsDir -Force
$fontCount = (Get-ChildItem $fontsDir -Filter "*.ttf").Count
Write-Host "[OK] Copied $fontCount TrueType fonts into deploy\fonts"

# 4. Copy logo and icon assets
Copy-Item "E:\StickCore\media\stickcore.ico" "$deployDir\stickcore.ico" -Force
Copy-Item "E:\StickCore\media\stickcore_logo.png" "$deployDir\stickcore_logo.png" -Force
Copy-Item "E:\StickCore\media\stickcore_badge.png" "$deployDir\stickcore_badge.png" -Force
Write-Host "[OK] Copied branding and icon assets"

# 4b. Copy unified Design Library (900+ motifs, thumbnails and index)
$deployLibDir = "$deployDir\library"
if (!(Test-Path $deployLibDir)) { New-Item -ItemType Directory -Path $deployLibDir | Out-Null }
Copy-Item "E:\StickCore\library\*" $deployLibDir -Recurse -Force
$scdCount = (Get-ChildItem "$deployLibDir\designs" -Filter "*.scd").Count
Write-Host "[OK] Copied $scdCount embroidery designs and thumbnails into deploy\library"

# 4c. Copy JEF embroidery files into deploy\output
$deployOutDir = "$deployDir\output"
if (!(Test-Path $deployOutDir)) { New-Item -ItemType Directory -Path $deployOutDir | Out-Null }
Get-ChildItem "E:\StickCore\output" -Filter "*.jef" -File | ForEach-Object {
    Copy-Item $_.FullName "$deployOutDir\$($_.Name)" -Force
}
foreach ($sub in @("fw", "EmbF5", "mef_jef")) {
    if (Test-Path "E:\StickCore\output\$sub") {
        $destSub = Join-Path $deployOutDir $sub
        if (!(Test-Path $destSub)) { New-Item -ItemType Directory -Path $destSub | Out-Null }
        Copy-Item "E:\StickCore\output\$sub\*" $destSub -Recurse -Force
    }
}
$jefCount = (Get-ChildItem $deployOutDir -Recurse -Include "*.jef", "*.JEF" -File).Count
Write-Host "[OK] Copied $jefCount JEF embroidery files into deploy\output"

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

# 6. Create 1-Click Desktop App Setup script
$setupBat = @'
@echo off
chcp 65001 >nul
echo ===================================================
echo     StickCore Studio v1.0.2 - Setup & Installation
echo ===================================================
echo.

set "TARGET_DIR=%LOCALAPPDATA%\Programs\StickCore"
echo [1/3] Kopiere Programmdateien nach %TARGET_DIR%...
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
xcopy /E /I /Y "%~dp0*" "%TARGET_DIR%\" >nul

echo [2/3] Registriere Desktop- & Startmenue-Verknuepfungen...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut([Environment]::GetFolderPath('Desktop') + '\StickCore Studio.lnk'); $s.TargetPath = '%TARGET_DIR%\StickCore.exe'; $s.WorkingDirectory = '%TARGET_DIR%'; $s.IconLocation = '%TARGET_DIR%\stickcore.ico,0'; $s.Description = 'StickCore Studio - Professionelle Sticksoftware'; $s.Save()"

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

# Portable launcher
$runBat = @'
@echo off
start "" "%~dp0StickCore.exe" %*
'@
Set-Content -Path "$deployDir\StickCore.bat" -Value $runBat -Encoding OEM

# 7. Build Inno Setup Installer (StickCoreSetup.exe)
$iscc = "C:\Users\micbu\AppData\Local\Programs\Inno Setup 6\ISCC.exe"
if (!(Test-Path $iscc)) {
    $iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
}
if (Test-Path $iscc) {
    $setupExe = "$outputDir\StickCoreSetup.exe"
    $rootSetupExe = "E:\StickCore\StickCoreSetup.exe"
    if (Test-Path $setupExe) { Remove-Item $setupExe -Force }
    if (Test-Path $rootSetupExe) { Remove-Item $rootSetupExe -Force }
    Write-Host "Building Windows Installer (StickCoreSetup.exe) with Inno Setup 6..." -ForegroundColor Cyan
    & $iscc "E:\StickCore\installer\StickCore.iss"
    if (Test-Path $setupExe) {
        Copy-Item $setupExe $rootSetupExe -Force
        & "C:\Program Files\Windows Defender\MpCmdRun.exe" -Scan -ScanType 3 -File $setupExe
        $setupMb = [math]::Round((Get-Item $setupExe).Length / 1MB, 2)
        Write-Host "[OK] Created Installer: $setupExe ($setupMb MB)" -ForegroundColor Green
    }
}

# 8. Create Portable ZIP packages
$zipFile102 = "$outputDir\StickCore-Portable-v1.0.2.zip"
$zipFile101 = "$outputDir\StickCore-Portable-v1.0.1.zip"
if (Test-Path $zipFile102) { Remove-Item $zipFile102 -Force }
Write-Host "Creating ZIP archive: $zipFile102..."
Compress-Archive -Path "$deployDir\*" -DestinationPath $zipFile102 -Force
Copy-Item $zipFile102 $zipFile101 -Force
$zipLen = (Get-Item $zipFile102).Length
$zipSizeMb = [math]::Round($zipLen / 1MB, 2)
Write-Host "[OK] Created ZIP package: $zipFile102 ($zipSizeMb MB)" -ForegroundColor Green

Write-Host "=== Package & Installer Assembly Complete! ===" -ForegroundColor Cyan
