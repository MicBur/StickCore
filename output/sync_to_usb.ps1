# StickCore USB Auto-Sync for Kieler Wappen
$usb = Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -like "F:\*" -or ($_.Description -match "Removable" -and $_.Root -ne "C:\") } | Select-Object -First 1
if ($usb) {
    $drive = $usb.Root
    Write-Host "USB Stick erkannt auf $drive"
    $targetDirs = @(
        "$drive",
        "$drive\EMB\MyDesign",
        "$drive\EmbF5\MyDesign",
        "$drive\MyDesign",
        "$drive\fw"
    )
    foreach ($td in $targetDirs) {
        if (!(Test-Path $td)) { New-Item -ItemType Directory -Path $td -Force | Out-Null }
        Copy-Item -Path "E:\StickCore\output\*.jef" -Destination $td -Force
        Write-Host "Kopiert nach $td"
    }
    Write-Host "USB-Sync erfolgreich abgeschlossen!"
} else {
    Write-Host "Kein USB-Stick eingelegt."
}
