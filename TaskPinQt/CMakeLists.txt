cmake_minimum_required(VERSION 3.20)
project(TaskPinQt VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    enable_language(RC)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

include(CTest)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network PrintSupport Svg)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    qt_add_executable(TaskPinScreenshotTests
        tests/tst_screenshot.cpp
        src/ui/SnipOverlay.h src/ui/SnipOverlay.cpp
        src/ui/SnipToolbar.h src/ui/SnipToolbar.cpp src/ui/EditorHistory.h
        src/ui/Annotation.h src/ui/Annotation.cpp
        src/ui/IconSet.h src/ui/IconSet.cpp
        src/core/Settings.h src/core/Settings.cpp
        src/core/InstanceNames.h src/core/InstanceNames.cpp
        src/core/FileNaming.h src/core/FileNaming.cpp
        src/platform/WindowPicker.h src/platform/WindowPicker.cpp)
    target_include_directories(TaskPinScreenshotTests PRIVATE src)
    target_link_libraries(TaskPinScreenshotTests PRIVATE Qt6::Test Qt6::Widgets Qt6::Svg user32 dwmapi)
    add_test(NAME TaskPinScreenshotTests COMMAND TaskPinScreenshotTests -o -,txt
        -o "${CMAKE_BINARY_DIR}/TaskPinScreenshotTests.txt,txt"
        -o "${CMAKE_BINARY_DIR}/TaskPinScreenshotTests.xml,junitxml")
    set_tests_properties(TaskPinScreenshotTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(TaskPinIntegrationTests
        tests/tst_integration.cpp)
    target_include_directories(TaskPinIntegrationTests PRIVATE src)
    target_link_libraries(TaskPinIntegrationTests PRIVATE Qt6::Test Qt6::Widgets user32 dwmapi)
    add_test(NAME TaskPinIntegrationTests COMMAND TaskPinIntegrationTests -o -,txt
        -o "${CMAKE_BINARY_DIR}/TaskPinIntegrationTests.txt,txt"
        -o "${CMAKE_BINARY_DIR}/TaskPinIntegrationTests.xml,junitxml")
    set_tests_properties(TaskPinIntegrationTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

qt_add_executable(TaskPinQt WIN32
    src/main.cpp
    src/ui/AppIcon.h assets/brand/brand.qrc

    src/core/Task.h
    src/core/Store.h              src/core/Store.cpp
    src/core/Settings.h           src/core/Settings.cpp
    src/core/InstanceNames.h      src/core/InstanceNames.cpp
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
    src/ui/IconSet.h              src/ui/IconSet.cpp
    src/ui/SnipOverlay.h          src/ui/SnipOverlay.cpp
    src/ui/SnipToolbar.h src/ui/SnipToolbar.cpp src/ui/EditorHistory.h
    src/ui/CardWindow.h           src/ui/CardWindow.cpp
    src/ui/CardAppearance.h       src/ui/TaskDialogs.h src/ui/TaskDialogs.cpp
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
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::PrintSupport Qt6::Svg
    user32 gdi32 shell32 dwmapi
)

if(BUILD_TESTING)
    # Exercise the real host and widgets without the application's main entry point.
    get_target_property(TASKPIN_APP_SOURCES TaskPinQt SOURCES)
    list(FILTER TASKPIN_APP_SOURCES EXCLUDE REGEX "(^|/)main\\.cpp$")
    target_sources(TaskPinIntegrationTests PRIVATE ${TASKPIN_APP_SOURCES})
    target_compile_definitions(TaskPinIntegrationTests PRIVATE TASKPIN_VERSION="${PROJECT_VERSION}")
    target_link_libraries(TaskPinIntegrationTests PRIVATE
        Qt6::Sql Qt6::Network Qt6::PrintSupport Qt6::Svg gdi32 shell32)
    add_dependencies(TaskPinIntegrationTests TaskPinQt)
    qt_add_executable(TaskPinExperienceTests tests/tst_experience.cpp ${TASKPIN_APP_SOURCES})
    target_include_directories(TaskPinExperienceTests PRIVATE src)
    target_compile_definitions(TaskPinExperienceTests PRIVATE TASKPIN_VERSION="${PROJECT_VERSION}")
    target_link_libraries(TaskPinExperienceTests PRIVATE Qt6::Test Qt6::Widgets Qt6::Sql Qt6::Network
        Qt6::PrintSupport Qt6::Svg user32 gdi32 shell32 dwmapi)
    add_dependencies(TaskPinExperienceTests TaskPinIntegrationTests)
    add_test(NAME TaskPinExperienceTests COMMAND TaskPinExperienceTests -o -,txt
        -o "${CMAKE_BINARY_DIR}/TaskPinExperienceTests.txt,txt"
        -o "${CMAKE_BINARY_DIR}/TaskPinExperienceTests.xml,junitxml")
    set_tests_properties(TaskPinExperienceTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# copy the Qt runtime next to the exe (windeployqt) after build
if(WIN32)
    set_source_files_properties(assets/brand/taskpin.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/taskpin.ico")
    target_sources(TaskPinQt PRIVATE assets/brand/taskpin.rc)
    target_include_directories(TaskPinQt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand")
    find_program(WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET TaskPinQt POST_BUILD
            COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:TaskPinQt>"
            COMMENT "windeployqt")
        if(BUILD_TESTING)
            foreach(test_target TaskPinScreenshotTests TaskPinIntegrationTests TaskPinExperienceTests)
                add_custom_command(TARGET ${test_target} POST_BUILD
                    COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                            --no-opengl-sw "$<TARGET_FILE:${test_target}>"
                    COMMENT "Deploy test Qt runtime")
            endforeach()
        endif()
    endif()
    if(BUILD_TESTING)
        find_file(QT_OFFSCREEN_PLUGIN qoffscreen.dll HINTS "${Qt6_DIR}/../../../plugins/platforms" NO_DEFAULT_PATH)
        foreach(test_target TaskPinScreenshotTests TaskPinIntegrationTests TaskPinExperienceTests)
            add_custom_command(TARGET ${test_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>"
                        "$<TARGET_FILE_DIR:${test_target}>")
            if(QT_OFFSCREEN_PLUGIN)
                add_custom_command(TARGET ${test_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${test_target}>/platforms"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QT_OFFSCREEN_PLUGIN}"
                            "$<TARGET_FILE_DIR:${test_target}>/platforms")
            endif()
        endforeach()
    endif()
endif()
