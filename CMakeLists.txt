cmake_minimum_required(VERSION 3.20)
project(SteamControllerRemapper VERSION 1.4.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(cmake/SDLVersion.cmake)

option(SCR_FETCH_SDL "Fetch the pinned SDL dependency for future input integration" ON)

if (SCR_FETCH_SDL)
    set(SCR_SDL_GIT_TAG "release-${SCR_SDL_VERSION}")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${SCR_SDL_GIT_TAG}
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# Shared HID + Steam Controller library code
add_library(SteamController STATIC
    src/hid/HidDevice.cpp
    src/steam/SteamController.cpp
)
target_include_directories(SteamController PUBLIC src)
target_link_libraries(SteamController PUBLIC hid setupapi)
if (MSVC)
    target_compile_options(SteamController PRIVATE /W4 /WX)
endif()

# Diagnostic probe tool (console)
add_executable(SteamProbe
    src/main.cpp
)
target_link_libraries(SteamProbe PRIVATE SteamController)
if (MSVC)
    target_compile_options(SteamProbe PRIVATE /W4 /WX)
    set_target_properties(SteamProbe PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
endif()

# RawGameController diagnostic probe (WinRT)
add_executable(RawControllerProbe
    src/probe/RawControllerProbe.cpp
)
target_link_libraries(RawControllerProbe PRIVATE windowsapp)
if (MSVC)
    target_compile_options(RawControllerProbe PRIVATE /W4)
endif()

# System tray app (no console window)
add_executable(SteamControllerRemapper WIN32
    src/app/main.cpp
    src/app/TrayApp.cpp
    src/app/RemapBackend.cpp
    src/app/RemapIpcServer.cpp
    src/app/ControllerManager.cpp
    src/app/SdlGamepadInput.cpp
    src/app/VirtualController.cpp
    src/app/PaddleOverlay.cpp
    src/app/PaddleConfig.cpp
    src/app/PaddleConfigWindow.cpp
    src/app/SteamLibrary.cpp
    src/app/MacroRecorder.cpp
    src/app/TrackpadMouse.cpp
    resources/app.rc
)
target_include_directories(SteamControllerRemapper PRIVATE resources third_party)
target_link_libraries(SteamControllerRemapper PRIVATE SteamController gdiplus ole32 comctl32)
target_link_libraries(SteamControllerRemapper PRIVATE winhttp)
target_link_libraries(SteamControllerRemapper PRIVATE urlmon)
if (TARGET SDL3::SDL3)
    target_link_libraries(SteamControllerRemapper PRIVATE SDL3::SDL3)
endif()
target_compile_definitions(SteamControllerRemapper PRIVATE SCR_APP_VERSION="${PROJECT_VERSION}")
set_target_properties(SteamControllerRemapper PROPERTIES
    OUTPUT_NAME "Steam Controller Remapper"
)
if (MSVC)
    target_compile_options(SteamControllerRemapper PRIVATE /W4 /WX)
endif()

add_custom_command(TARGET SteamControllerRemapper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/libVIIPER/libVIIPER.dll"
        "$<TARGET_FILE_DIR:SteamControllerRemapper>/libVIIPER.dll"
)
