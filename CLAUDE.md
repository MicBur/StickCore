# StickCore Studio – Project Instructions & Agent Guidelines

## 🧵 Project Overview
StickCore Studio is a modern, high-precision embroidery digitizer and design suite built with **C++20** and **Qt 6.11** (MinGW 64-bit). It provides full feature parity with industrial software like Janome Digitizer Jr, native JEF & Tajima DST export, real-time 2D design canvas with artwork dimming, 3D OpenGL thread simulation, and vector digitizing (including authentic satin badge reproduction for Modewerkstatt Knüppel).

---

## 🛠️ Build & Environment Toolchain

### Windows MinGW64 Environment
```powershell
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;" + $env:PATH
```

### Build Commands
```powershell
# Configure & Build
cmake -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw -j8

# Run Application
.\build-mingw\StickCore.exe

# Run Unit Tests (Mandatory before commits)
.\build-mingw\StickCoreTests.exe
```

### Installer Compilation (Inno Setup 6)
```powershell
Copy-Item build-mingw\StickCore.exe installer\deploy\StickCore.exe -Force
& "C:\Users\micbu\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer\StickCore.iss
# Output: installer\Output\StickCoreSetup.exe
```

### PDF Documentation Generation (Google Chrome Headless)
```powershell
Start-Process -FilePath "C:\Program Files\Google\Chrome\Application\chrome.exe" -ArgumentList "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--print-to-pdf=E:\StickCore\docs\StickCore_Benutzerhandbuch.pdf", "--user-data-dir=E:\StickCore\build-mingw\chrome-profile", "file:///E:/StickCore/docs/StickCore_Benutzerhandbuch.html" -Wait
```

---

## 🏛️ Architecture & Key Components

```
src/
├── codec/           # Binary stitch codecs
│   ├── JefCodec.*   # Janome MC350E binary format (Header 0x14, dist bounds, color tables)
│   └── DstCodec.*   # Tajima ternary differencing encoder/decoder
├── core/            # Fundamental stitch primitives & machine configurations
│   ├── StitchTypes.h# Stitch (x, y, flags, colorIdx), StitchSequence, SF_Jump/Normal/ColorChange
│   ├── Geometry.h   # 2D vector math & arc-length parameterization
│   ├── ThreadCatalog# Thread matching (Madeira, Janome, Robison-Anton, Mettler)
│   ├── MachineProfile# Machine bounds, hoop dimensions (Hoop A, B, C, SQ14, D)
│   └── SvgPathParser# Robust SVG 'd' string and XML path parser
├── editor/          # 2D Canvas & Vector editing
│   ├── PathEditorWidget.* # Dual-mode 2D Canvas & Bézier tool
│   └── BezierPath.h # Cubic Bézier curve model with handles
├── generators/      # Embroidery algorithms
│   ├── SatinGenerator.*   # Orthogonal zigzag satin columns with pull compensation
│   ├── TatamiFill.*       # Scanline fill with angle rotation & phase offset
│   ├── SvgDigitizer.*     # AuthenticPatchSatin, ContourEcho, TatamiWeave, RoyalDuotone
│   ├── LogoGenerator.*    # Modewerkstatt Knüppel authentic badge & lettering
│   ├── TextDigitizer.*    # Arc Up / Arc Down / Straight / Circle lettering with kerning & slant
│   ├── AppliqueGenerator  # 3-stage appliqué (Placement, Tackdown, Satin cover)
│   ├── MonogramGenerator  # Intertwined artistic monograms with frame
│   ├── ImageDigitizer.*   # Auto-digitizing raster images to stitches
│   └── Underlay.*         # Center-walk, edge-walk and cross-hatch underlays
├── gl/              # 3D OpenGL 3.3 Core Profile simulation
│   └── StitchGLWidget.*   # Instanced cylinder threads, Blinn-Phong lighting, fabric textures
├── library/         # Design library management & starter patterns
├── net/             # Embedded offline QR-upload HTTP server (local Wi-Fi)
│   ├── QrUploadServer.*   # Multi-part upload handler for smartphone gallery/camera
│   └── QrCode.*           # Standalone QR matrix generator
├── ui/              # Modal dialogues (Lettering, Placement, ThreadPick, Image, Wizard)
├── MainWindow.*     # Central UI coordinator, menus, status bar, actions
└── main.cpp         # Entry point, theme styling & CLI headless screenshot capture
```

---

## 📐 Core Embroidery Specifications

1. **Janome JEF File Format Standards:**
   * Header Version: `0x14`
   * Stitch Offset: `0x74 + colorCount * 8` (two 4-byte tables: Janome Thread Code + Attributes)
   * Reference byte: `0x16 == 0x64`
   * Coordinate units: `0.1 mm` (tenth mm), `Y` axis inverted relative to screen
   * End marker: `0x80 0x10 0x00 0x00`
   * Hoops: Hoop A (126×110 mm, Code 0), Hoop B (140×200 mm, Code 2), Hoop C (50×50 mm, Code 1)
2. **Satin Column Generation:**
   * Stitch pitch / density: typically `0.30 - 0.40 mm`
   * Pull compensation: `+0.15 mm` on outer boundaries
   * Max stitch length: `12.0 mm` (splits automatically to prevent loose thread snags)
   * Underlay: Center-track and edge-walk passes before cover satin
3. **Tatami Scanline Fill:**
   * Scanline spacing: `0.30 - 0.45 mm`
   * Max stitch length: `2.5 - 4.0 mm`
   * Phase fraction: `0.25 - 0.33` to stagger needle penetrations and eliminate vertical grooves

---

## ⚡ Engineering Directives (Elite Protocol)

1. **Zero-Rush & Full Execution:** Never use placeholders, `// TODO`, or incomplete implementations. Every file must be complete, compilable, and production-ready.
2. **Mandatory Verification:** Never mark a task as complete without building the code and running `StickCoreTests.exe`. Ensure 100% pass rate.
3. **Memory Integration:** Always record milestones, architectural decisions, and credentials in the central Hermes memory store (`C:\Users\micbu\Documents\mem\memory\`).
4. **Clean Git Hygiene:** Keep the repository clean of build folders, temporary debug binaries, and installer outputs. Maintain comprehensive documentation.
