@echo off
setlocal EnableExtensions
set QT_ROOT=D:/AutoEx/Qt/Qt_6.4.3
set QT_KIT=%QT_ROOT%/6.4.3/mingw_64
set MINGW=%QT_ROOT%/Tools/mingw1120_64
set CMAKE=%QT_ROOT%/Tools/CMake_64/bin/cmake.exe
set NINJA=%QT_ROOT%/Tools/Ninja/ninja.exe
set SRC=%~dp0
set BUILD=%~dp0build

set PATH=%MINGW%\bin;%QT_KIT%\bin;%QT_ROOT%\Tools\Ninja;%PATH%

if not exist "%BUILD%" mkdir "%BUILD%"

rem C++ only: no CMAKE_C_COMPILER here, otherwise CMake warns about an unused variable
"%CMAKE%" -S "%SRC%." -B "%BUILD%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=%MINGW%/bin/g++.exe -DCMAKE_PREFIX_PATH=%QT_KIT%
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILD%"
if errorlevel 1 exit /b 1

echo.
echo == done: %BUILD%TaskPinQt.exe ==
endlocal
