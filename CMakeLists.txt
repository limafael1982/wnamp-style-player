cmake_minimum_required(VERSION 3.20)
project(wnamp LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project targets native Windows (Win32) only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- miniz (MIT) for .wsz (zip) reading ---------------------------------
include(FetchContent)
# miniz 3.0.2 declares cmake_minimum_required < 3.5, which CMake 4 rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AMALGAMATE_SOURCES OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG        3.0.2)
FetchContent_MakeAvailable(miniz)

# --- audio library: the ONLY target that sees GStreamer headers ------------
option(WNAMP_GSTREAMER "Build the GStreamer audio engine" ON)
add_library(wnamp_audio STATIC
  src/audio/IEngine.h
  src/audio/Engine.h
  src/audio/EngineFactories.h
  src/audio/Engine.cpp
  src/audio/NullEngine.cpp)
target_include_directories(wnamp_audio PUBLIC src)
target_compile_definitions(wnamp_audio PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(wnamp_audio PRIVATE /W4 /utf-8 /permissive-)
endif()

if(WNAMP_GSTREAMER)
  # Official MSVC installer sets GSTREAMER_1_0_ROOT_MSVC_X86_64; override with -DGSTREAMER_ROOT=...
  set(GSTREAMER_ROOT "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}" CACHE PATH "GStreamer MSVC root")
  if(NOT GSTREAMER_ROOT OR NOT EXISTS "${GSTREAMER_ROOT}/include/gstreamer-1.0")
    message(FATAL_ERROR "GStreamer MSVC SDK not found. Install the official runtime + development "
                        "installers, or set -DGSTREAMER_ROOT=... or -DWNAMP_GSTREAMER=OFF.")
  endif()
  target_sources(wnamp_audio PRIVATE src/audio/GStreamerEngine.cpp)
  target_compile_definitions(wnamp_audio PUBLIC WNAMP_HAVE_GSTREAMER)
  target_include_directories(wnamp_audio PRIVATE
    "${GSTREAMER_ROOT}/include/gstreamer-1.0"
    "${GSTREAMER_ROOT}/include/glib-2.0"
    "${GSTREAMER_ROOT}/lib/glib-2.0/include")
  target_link_directories(wnamp_audio PUBLIC "${GSTREAMER_ROOT}/lib")
  target_link_libraries(wnamp_audio PUBLIC gstreamer-1.0 gstpbutils-1.0 gobject-2.0 glib-2.0)
endif()

# --- app -----------------------------------------------------------------
add_executable(wnamp WIN32
  src/main.cpp
  src/skin/Skin.h
  src/skin/Skin.cpp
  src/skin/Sprites.h
  src/ui/MainWindow.h
  src/ui/MainWindow.cpp
  src/ui/EqWindow.h
  src/ui/EqWindow.cpp
  src/ui/PlaylistWindow.h
  src/ui/PlaylistWindow.cpp
  src/playlist/Playlist.h
  src/playlist/Playlist.cpp
  src/playlist/PlaylistIo.h
  src/playlist/PlaylistIo.cpp
  src/settings/Settings.h
  src/settings/Settings.cpp
  src/ui/SkinBrowser.h
  src/ui/SkinBrowser.cpp
  src/ui/SettingsWindow.h
  src/ui/SettingsWindow.cpp
  src/net/Http.h
  src/net/Http.cpp
  src/net/SomaFm.h
  src/net/SomaFm.cpp
  src/radio/Station.h
  src/radio/Stations.h
  src/radio/RadioMenu.h
  src/radio/RadioMenu.cpp
  resources/app.manifest
  resources/app.rc)

target_include_directories(wnamp PRIVATE src)
target_compile_definitions(wnamp PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wnamp PRIVATE wnamp_audio miniz user32 gdi32 ole32 windowscodecs comdlg32 shell32 uuid winhttp)
if(MSVC)
  target_compile_options(wnamp PRIVATE /W4 /utf-8 /permissive-)
endif()

# Ship the built-in default skin next to the exe (loaded when no other skin is given).
if(EXISTS "${CMAKE_SOURCE_DIR}/data/default.wsz")
  add_custom_command(TARGET wnamp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:wnamp>/skins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/data/default.wsz"
            "$<TARGET_FILE_DIR:wnamp>/skins/default.wsz")
endif()
