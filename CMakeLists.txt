cmake_minimum_required(VERSION 3.16)
project(StickCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 6 (Widgets pulls in Gui/Core; OpenGLWidgets is separate in Qt 6).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)

# OpenCV is OPTIONAL. When found, StickCore gets high-quality photo analysis
# (face detection, Canny, adaptive threshold, comic stylise); when absent it
# falls back to the native PhotoProcessor. Toggle with -DSTICK_USE_OPENCV=OFF.
option(STICK_USE_OPENCV "Build with OpenCV photo analysis" ON)
set(STICK_OPENCV_QRC "")
set(STICK_HAVE_OPENCV OFF)
if(STICK_USE_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc objdetect photo)
    if(OpenCV_FOUND)
        message(STATUS "StickCore: OpenCV ${OpenCV_VERSION} found — photo AI enabled")
        set(STICK_HAVE_OPENCV ON)
        set(STICK_OPENCV_QRC cvdata.qrc)
    else()
        message(STATUS "StickCore: OpenCV not found — using native photo fallback")
    endif()
endif()

add_executable(StickCore WIN32
    media.qrc
    fonts.qrc
    ${STICK_OPENCV_QRC}
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/core/StitchTypes.h
    src/core/Geometry.h
    src/core/MachineProfile.h
    src/core/ThreadCatalog.cpp
    src/core/ThreadCatalog.h
    src/generators/SatinGenerator.cpp
    src/generators/SatinGenerator.h
    src/generators/TatamiFill.cpp
    src/generators/TatamiFill.h
    src/generators/Underlay.cpp
    src/generators/Underlay.h
    src/generators/TextDigitizer.cpp
    src/generators/TextDigitizer.h
    src/generators/MonogramGenerator.cpp
    src/generators/MonogramGenerator.h
    src/generators/ContourFill.cpp
    src/generators/ContourFill.h
    src/generators/LogoGenerator.cpp
    src/generators/LogoGenerator.h
    src/generators/SvgDigitizer.cpp
    src/generators/SvgDigitizer.h
    src/core/SvgPathParser.h
    src/generators/ImageDigitizer.cpp
    src/generators/ImageDigitizer.h
    src/generators/PhotoProcessor.cpp
    src/generators/PhotoProcessor.h
    src/generators/OpenCvBridge.cpp
    src/generators/AppliqueGenerator.cpp
    src/generators/AppliqueGenerator.h
    src/codec/DstCodec.cpp
    src/codec/DstCodec.h
    src/ui/ThreadPickDialog.cpp
    src/ui/ThreadPickDialog.h
    src/ui/ImageDialog.cpp
    src/ui/ImageDialog.h
    src/ui/PlacementDialog.cpp
    src/ui/PlacementDialog.h
    src/codec/JefCodec.cpp
    src/codec/JefCodec.h
    src/library/StitchThumbnail.h
    src/library/DesignFactory.cpp
    src/library/DesignFactory.h
    src/library/DesignLibrary.cpp
    src/library/DesignLibrary.h
    src/library/LibraryDialog.cpp
    src/library/LibraryDialog.h
    src/ui/WelcomeWizard.cpp
    src/ui/WelcomeWizard.h
    src/net/QrCode.cpp
    src/net/QrCode.h
    src/net/QrUploadServer.cpp
    src/net/QrUploadServer.h
    src/gl/StitchGLWidget.cpp
    src/gl/StitchGLWidget.h
    src/editor/BezierPath.h
    src/editor/PathEditorWidget.cpp
    src/editor/PathEditorWidget.h
)

target_include_directories(StickCore PRIVATE src)

target_link_libraries(StickCore PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
)

if(STICK_HAVE_OPENCV)
    target_compile_definitions(StickCore PRIVATE STICK_HAVE_OPENCV)
    target_link_libraries(StickCore PRIVATE ${OpenCV_LIBS})
    target_include_directories(StickCore PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

# Install rule (used by the Windows packaging workflow + windeployqt).
install(TARGETS StickCore RUNTIME DESTINATION . BUNDLE DESTINATION .)

# ---------------------------------------------------------------------------
# Headless verification of the non-GL core (Satin / Tatami / JEF / DST / Applique).
# Run:  ./build/StickCoreTests  [pfad/zu/referenz.jef]
# ---------------------------------------------------------------------------
add_executable(StickCoreTests
    tests/verify.cpp
    media.qrc
    src/core/ThreadCatalog.cpp
    src/generators/SatinGenerator.cpp
    src/generators/TatamiFill.cpp
    src/generators/Underlay.cpp
    src/generators/AppliqueGenerator.cpp
    src/generators/TextDigitizer.cpp
    src/generators/MonogramGenerator.cpp
    src/generators/ContourFill.cpp
    src/generators/LogoGenerator.cpp
    src/generators/SvgDigitizer.cpp
    src/generators/ImageDigitizer.cpp
    src/generators/PhotoProcessor.cpp
    src/generators/OpenCvBridge.cpp
    src/codec/JefCodec.cpp
    src/codec/DstCodec.cpp
    src/library/DesignFactory.cpp
    src/library/DesignLibrary.cpp
    src/net/QrCode.cpp
    src/net/QrUploadServer.cpp
    ${STICK_OPENCV_QRC}
)
target_include_directories(StickCoreTests PRIVATE src)
target_link_libraries(StickCoreTests PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
if(STICK_HAVE_OPENCV)
    target_compile_definitions(StickCoreTests PRIVATE STICK_HAVE_OPENCV)
    target_link_libraries(StickCoreTests PRIVATE ${OpenCV_LIBS})
    target_include_directories(StickCoreTests PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

# Integration test for the phone-upload HTTP server (needs curl at runtime).
add_executable(StickCoreServerTest tests/servertest.cpp src/net/QrUploadServer.cpp)
target_include_directories(StickCoreServerTest PRIVATE src)
target_link_libraries(StickCoreServerTest PRIVATE Qt6::Core Qt6::Gui Qt6::Network)

enable_testing()
add_test(NAME StickCoreTests COMMAND StickCoreTests)
add_test(NAME StickCoreServerTest COMMAND StickCoreServerTest)
