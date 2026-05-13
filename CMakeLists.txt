cmake_minimum_required(VERSION 3.15)
project(ScreenDoodle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# No embedded manifest — DPI awareness is set programmatically and we use
# custom-painted widgets, so we don't need Comctl32 v6 visual styles. This
# avoids the duplicate-MANIFEST resource clashes on MSVC.
add_executable(ScreenDoodle WIN32
    src/main.cpp
    src/state.cpp
    src/canvas.cpp
    src/widget.cpp
    src/settings.cpp
    src/tray.cpp
    resources/app.rc
)

target_include_directories(ScreenDoodle PRIVATE src resources)

target_compile_definitions(ScreenDoodle PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MINGW)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s -DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -mwindows -municode -static -static-libgcc -static-libstdc++")
    add_compile_options(-municode)
endif()

if(MSVC)
    target_compile_options(ScreenDoodle PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(ScreenDoodle PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup)
endif()

target_link_libraries(ScreenDoodle PRIVATE
    gdiplus
    gdi32
    user32
    shell32
    comctl32
    msimg32
    ole32
    advapi32
)
