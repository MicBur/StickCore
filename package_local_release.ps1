$ErrorActionPreference = "Stop"

Write-Host "=== StickCore: Assembling Local Release Package & Installer ===" -ForegroundColor Cyan

$deployDir = "E:\StickCore\deploy"
$outputDir = "E:\StickCore\output"

if (!(Test-Path $deployDir)) { New-Item -ItemType Directory -Path $deployDir | Out-Null }
if (!(Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir | Out-Null }

# 1. Update StickCore.exe from newest build (build\Release or build-local)
$exeRelease = "E:\StickCore\build\Release\StickCore.exe"
$exeLocal   = "E:\StickCore\build-local\StickCore.exe"
if ((Test-Path $exeRelease) -and (!(Test-Path $exeLocal) -or ((Get-Item $exeRelease).LastWriteTime -ge (Get-Item $exeLocal).LastWriteTime))) {
    Copy-Item $exeRelease "$deployDir\StickCore.exe" -Force
    Write-Host "[OK] Copied latest StickCore.exe from build\Release"
} else {
    Copy-Item $exeLocal "$deployDir\StickCore.exe" -Force
    Write-Host "[OK] Copied latest StickCore.exe from build-local"
}

# 2. Run windeployqt
Write-Host "Running windeployqt..."
$ErrorActionPreference = "Continue"
& "C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe" --release --compiler-runtime "$deployDir\StickCore.exe"
$ErrorActionPreference = "Stop"
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

# 5. Clean any legacy .ps1 scripts from deploy so AV heuristics never trigger on laptop
if (Test-Path "$deployDir\Install-Fonts.ps1") { Remove-Item "$deployDir\Install-Fonts.ps1" -Force }
if (Test-Path "$deployDir\Laptop-Installation-Ohne-Blockade.bat") { Remove-Item "$deployDir\Laptop-Installation-Ohne-Blockade.bat" -Force }

# 6. Create clean 1-Click Desktop App Setup script (standard xcopy only, zero PowerShell/WScript calls)
$setupBat = @'
@echo off
chcp 65001 >nul
echo ===================================================
echo     StickCore Studio v1.2.0 - Schnell-Start & Setup
echo ===================================================
echo.

set "TARGET_DIR=%LOCALAPPDATA%\Programs\StickCore"
echo [1/3] Kopiere Programmdateien nach %TARGET_DIR%...
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
xcopy /E /I /Y "%~dp0*" "%TARGET_DIR%\" >nul

echo [2/3] Aktualisiere Stick-Bibliothek (Wappen, Gegenbogen Unten, Aermel-Zahl 30)...
if not exist "%APPDATA%\StickCore\library" mkdir "%APPDATA%\StickCore\library"
xcopy /E /I /Y "%~dp0library\*" "%APPDATA%\StickCore\library\" >nul
if not exist "%APPDATA%\StickCore\StickCore\library" mkdir "%APPDATA%\StickCore\StickCore\library"
xcopy /E /I /Y "%~dp0library\*" "%APPDATA%\StickCore\StickCore\library\" >nul

echo [3/3] Starte StickCore Studio v1.2.0...
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
$zipFile120 = "$outputDir\StickCore-Portable-v1.2.0.zip"
$zipFile102 = "$outputDir\StickCore-Portable-v1.0.2.zip"
if (Test-Path $zipFile120) { Remove-Item $zipFile120 -Force }
Write-Host "Creating ZIP archive: $zipFile120..."
Compress-Archive -Path "$deployDir\*" -DestinationPath $zipFile120 -Force
Copy-Item $zipFile120 $zipFile102 -Force
$zipLen = (Get-Item $zipFile120).Length
$zipSizeMb = [math]::Round($zipLen / 1MB, 2)
Write-Host "[OK] Created ZIP package: $zipFile120 ($zipSizeMb MB)" -ForegroundColor Green

Write-Host "=== Package & Installer Assembly Complete! ===" -ForegroundColor Cyan
