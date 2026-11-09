cmake_minimum_required(VERSION 3.20)
project(TaskPinQt VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network PrintSupport)

qt_add_executable(TaskPinQt WIN32
    src/main.cpp

    src/core/Task.h
    src/core/Store.h              src/core/Store.cpp
    src/core/Settings.h           src/core/Settings.cpp
    src/core/SelfTest.h           src/core/SelfTest.cpp
    src/core/FileNaming.h         src/core/FileNaming.cpp
    src/core/TimeParser.h         src/core/TimeParser.cpp
    src/core/History.h            src/core/History.cpp
    src/core/Ocr.h                src/core/Ocr.cpp
    src/core/AiClient.h           src/core/AiClient.cpp

    src/platform/Hotkeys.h        src/platform/Hotkeys.cpp
    src/platform/HotkeyText.h     src/platform/HotkeyText.cpp
    src/platform/HotkeyEdit.h     src/platform/HotkeyEdit.cpp
    src/platform/ScreenCapture.h  src/platform/ScreenCapture.cpp
    src/platform/WindowPicker.h   src/platform/WindowPicker.cpp
    src/platform/WindowFx.h       src/platform/WindowFx.cpp
    src/platform/Autostart.h      src/platform/Autostart.cpp

    src/ui/Annotation.h           src/ui/Annotation.cpp
    src/ui/SnipOverlay.h          src/ui/SnipOverlay.cpp
    src/ui/CardWindow.h           src/ui/CardWindow.cpp
    src/ui/QuickAddDialog.h       src/ui/QuickAddDialog.cpp
    src/ui/PinWindow.h            src/ui/PinWindow.cpp
    src/ui/HistoryDialog.h        src/ui/HistoryDialog.cpp
    src/ui/CalendarWindow.h       src/ui/CalendarWindow.cpp
    src/ui/SettingsDialog.h       src/ui/SettingsDialog.cpp
    src/ui/ColorPickerOverlay.h   src/ui/ColorPickerOverlay.cpp
    src/ui/WhiteboardWindow.h     src/ui/WhiteboardWindow.cpp

    src/app/AppHost.h             src/app/AppHost.cpp
)

target_include_directories(TaskPinQt PRIVATE src)
target_compile_definitions(TaskPinQt PRIVATE TASKPIN_VERSION="${PROJECT_VERSION}")

target_link_libraries(TaskPinQt PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::PrintSupport
    user32 gdi32 shell32 dwmapi
)

# copy the Qt runtime next to the exe (windeployqt) after build
if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET TaskPinQt POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:TaskPinQt>"
            COMMENT "windeployqt")
    endif()
endif()
