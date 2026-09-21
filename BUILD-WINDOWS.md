# StickCore als Windows-App (Installer zum Doppelklicken)

Es gibt zwei Wege zu einer fertigen `StickCoreSetup.exe`. **Weg A** braucht keinen
eingerichteten Programmier-PC – ein kostenloses GitHub-Konto genügt, den Rest macht
GitHub automatisch. **Weg B** ist für den Fall, dass du einen Windows-PC direkt
einrichten willst.

---

## Weg A — GitHub baut den Installer für dich (empfohlen)

Du brauchst nur einen Browser. GitHub stellt dir kostenlose Windows-Rechner, die den
Installer bauen.

1. Kostenloses Konto anlegen auf **github.com**.
2. Oben rechts **„+" → „New repository"**. Namen vergeben (z. B. `stickcore`),
   **„Create repository"**.
3. Auf der neuen Seite **„uploading an existing file"** anklicken und den **kompletten
   Inhalt** des StickCore-Ordners hineinziehen (inklusive der versteckten Ordner
   `.github` und `installer`). Dann **„Commit changes"**.
4. Oben auf den Reiter **„Actions"** gehen. Dort läuft automatisch **„Windows
   Installer"** (dauert ca. 8–12 Minuten, grüner Haken = fertig).
5. Den fertigen Lauf anklicken → unten unter **„Artifacts"** liegt
   **`StickCore-Windows-Installer`**. Herunterladen, die ZIP entpacken →
   darin ist **`StickCoreSetup.exe`**.
6. `StickCoreSetup.exe` doppelklicken → installieren → StickCore starten.

> Beim ersten Start warnt Windows evtl. („unbekannter Herausgeber"), weil der
> Installer nicht kostenpflichtig signiert ist. Über **„Weitere Informationen →
> Trotzdem ausführen"** startet er normal.

---

## Weg B — Direkt auf einem Windows-PC bauen

1. **Qt** installieren: auf **qt.io** den Open-Source-Installer laden, bei den
   Komponenten **Qt 6.6 (oder neuer) mit „MSVC 2019 64-bit"** auswählen.
2. **Visual Studio Build Tools** (C++) und **CMake** installieren (CMake ist bei Qt
   oft dabei).
3. Das Startmenü öffnen und die **„Qt 6.x (MSVC 2019 64-bit)"**-Eingabeaufforderung
   starten. Dann im StickCore-Ordner:

   ```bat
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   mkdir deploy
   copy build\Release\StickCore.exe deploy\
   windeployqt --release deploy\StickCore.exe
   ```

4. In `deploy\` liegt jetzt **`StickCore.exe`** mit allen nötigen Dateien –
   startklar. Für einen echten Installer zusätzlich **Inno Setup** (jrsoftware.org)
   installieren und `installer\StickCore.iss` per Doppelklick → **„Compile"** bauen;
   das Ergebnis liegt in `Output\StickCoreSetup.exe`.

---

## Was der Installer bringt

- **StickCore** im Startmenü und (optional) auf dem Desktop.
- Alle Qt-Bibliotheken sind enthalten – kein separater Download nötig.
- Sauberes Deinstallieren über „Apps & Features".

Die App ist fest auf die **Janome MC350E** eingestellt (Rahmen A 126×110 und
B 140×200, JEF-Export, Garnfarben). Fertige `.jef`-Dateien überträgst du per
USB-Stick an die Maschine.
