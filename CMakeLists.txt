# CMakeLists.txt — Linux port (P0 scaffold, LINUX_PORT_PLAN §9.2)
# Windows continues to use roblox_external/roblox_external.vcxproj.
# This file is Linux-only and is not included in the vcxproj.
#
# CI: g++ -std=c++17 -fsyntax-only still works without cmake present.
# Once cmake is on Void (xbps-install cmake), this is the build entry:
#
#   cmake -B build-void -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-void -j$(nproc)
#   ./build-void/external --self-test   # P2 scanner check without overlay
#   ./build-void/external               # full overlay (P3)
#
# The Windows TU (overlay.cpp with DX11, nt_syscalls) are excluded here —
# platform/platform_linux.cpp + platform/sober_scanner.cpp are the Linux hal.

cmake_minimum_required(VERSION 3.16)
project(roblox-external VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the user forgot -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- ImGui core (vendored, backend-free) --------------------------------
# third_party/imgui is the stock ImGui 1.91.1 tree from glass-obsidian.
# We reuse it here so the overlay can use imgui_impl_sdl2/opengl3 without
# duplicating sources.
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/glass-obsidian/third_party/imgui")
set(IMGUI_SOURCES
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
)

# ---- Glass Obsidian (collapse-safe window + theme) ------------------------
set(GLASS_DIR "${CMAKE_SOURCE_DIR}/glass-obsidian")
set(GLASS_SOURCES
  "${GLASS_DIR}/src/glass.cpp"
  "${GLASS_DIR}/src/theme.cpp"
  "${GLASS_DIR}/src/palette.cpp"
  "${GLASS_DIR}/src/axiom.cpp"
  "${GLASS_DIR}/src/window.cpp"
)

# ---- Linux platform + scanner ---------------------------------------------
set(PLATFORM_SOURCES
  roblox_external/platform/platform_linux.cpp
  roblox_external/platform/sober_scanner.cpp
)

# ---- Core app (features, cache, game, memory) ------------------------------
# NOTE: overlay.cpp is Windows DX11 — excluded on Linux. overlay_linux.cpp
# is the SDL2+GL replacement (P3). Until P3 lands this target builds as
# a --self-test CLI that links without overlay.
set(CORE_SOURCES
  roblox_external/cache.cpp
  roblox_external/game.cpp
  roblox_external/memory.cpp
  roblox_external/process.cpp
  roblox_external/features/movement/mover.cpp
)

# ---- Overlay (P3) ----------------------------------------------------------
# When P3 lands, uncomment and add SDL2/OpenGL linking:
# set(OVERLAY_SOURCES roblox_external/overlay_linux.cpp)

add_executable(external
  ${IMGUI_SOURCES}
  ${GLASS_SOURCES}
  ${PLATFORM_SOURCES}
  ${CORE_SOURCES}
  # ${OVERLAY_SOURCES}
  roblox_external/main.cpp
)

target_include_directories(external PRIVATE
  roblox_external
  "${GLASS_DIR}/include"
  "${IMGUI_DIR}"
)

target_compile_options(external PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_compile_definitions(external PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# Linux system libs (always present on Void/X11)
find_package(Threads REQUIRED)
target_link_libraries(external PRIVATE Threads::Threads)

# P3 deps (opt-in until P3 — uncomment when SDL2 present on Void a/b):
# find_package(SDL2 QUIET)
# find_package(OpenGL QUIET)
# if(SDL2_FOUND AND OPENGL_FOUND)
#   target_link_libraries(external PRIVATE SDL2::SDL2 OpenGL::GL X11 Xtst)
#   target_sources(external PRIVATE roblox_external/overlay_linux.cpp)
#   target_compile_definitions(external PRIVATE HAS_SDL_OVERLAY=1)
# endif()

# Musl static fallback hint (void-b): user can pass -DMUSL_STATIC=ON
option(MUSL_STATIC "Link libstdc++/libgcc statically for musl Void" OFF)
if(MUSL_STATIC)
  target_link_options(external PRIVATE -static-libstdc++ -static-libgcc)
endif()

# ---- self-test helper ------------------------------------------------------
# A smaller target that only exercises sober_scanner without pulling main.cpp
add_executable(sober_probe EXCLUDE_FROM_ALL
  tools/sober_probe.cpp
  roblox_external/platform/platform_linux.cpp
  roblox_external/platform/sober_scanner.cpp
)
target_include_directories(sober_probe PRIVATE roblox_external)
target_link_libraries(sober_probe PRIVATE Threads::Threads)

# ---- install (for xbps-fetch courier payload) -------------------------------
install(TARGETS external DESTINATION bin)
