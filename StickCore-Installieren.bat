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
