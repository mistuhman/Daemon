# CMakeLists.txt — Linux port (P0→P2 plug-and-play + dynamic fetcher)
# Windows continues to use roblox_external/roblox_external.vcxproj.
# This file is Linux-only and not part of the vcxproj.

cmake_minimum_required(VERSION 3.16)
project(roblox-external VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- ImGui core (vendored) -------------------------------------------------
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/roblox_external/imgui")
set(IMGUI_SOURCES
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
)
set(IMGUI_BACKEND_SOURCES
  "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

# ---- Glass Obsidian -------------------------------------------------------
set(GLASS_DIR "${CMAKE_SOURCE_DIR}/glass-obsidian")
set(GLASS_SOURCES
  "${GLASS_DIR}/src/glass.cpp"
  "${GLASS_DIR}/src/theme.cpp"
  "${GLASS_DIR}/src/palette.cpp"
  "${GLASS_DIR}/src/axiom.cpp"
  "${GLASS_DIR}/src/window.cpp"
)

# ---- Linux platform + scanner (plug-and-play + dynamic offset fetcher) ---
set(PLATFORM_SOURCES
  roblox_external/platform/platform_linux.cpp
  roblox_external/platform/sober_scanner.cpp
  roblox_external/platform/port_watcher.cpp
  roblox_external/offsets_dynamic.cpp
  roblox_external/platform/overlay_linux.cpp
  roblox_external/platform/main_linux.cpp
  roblox_external/platform/config_linux.cpp
  roblox_external/platform/servers_http_linux.cpp
  roblox_external/platform/media_tracker_linux.cpp
  roblox_external/platform/streamproof_watcher.cpp
)

# ---- Core app (platform-free, same code as Windows) ----------------------
set(CORE_SOURCES
  roblox_external/cache.cpp
  roblox_external/game.cpp
  roblox_external/memory.cpp
  roblox_external/roblox_process.cpp
  roblox_external/features/movement/mover.cpp
  roblox_external/features/aimbot/aimbot.cpp
  roblox_external/features/antiafk/antiafk.cpp
  roblox_external/features/camera/camera_drive.cpp
  roblox_external/features/click_teleport/click_teleport.cpp
  roblox_external/features/esp/esp.cpp
  roblox_external/features/explorer/explorer.cpp
  roblox_external/features/flight/flight.cpp
  roblox_external/features/fling/fling.cpp
  roblox_external/features/fov_changer/fov_changer.cpp
  roblox_external/features/fpscap/fps_cap.cpp
  roblox_external/features/freecam/freecam.cpp
  roblox_external/features/gravity/gravity.cpp
  roblox_external/features/hipheight/hipheight.cpp
  roblox_external/features/hitbox_expander/hitbox_expander.cpp
  roblox_external/features/hitsound/hitsound.cpp
  roblox_external/features/infinite_jump/infinite_jump.cpp
  roblox_external/features/inventory_checker/inventory_checker.cpp
  roblox_external/features/jumppower/jumppower.cpp
  roblox_external/features/noclip/noclip.cpp
  roblox_external/features/nofall/nofall.cpp
  roblox_external/features/players/friends.cpp
  roblox_external/features/players/players.cpp
  roblox_external/features/raycast/world_ray.cpp
  roblox_external/features/servers/servers.cpp
  roblox_external/features/skybox_changer/skybox_changer.cpp
  roblox_external/features/triggerbot/triggerbot.cpp
  roblox_external/features/walkspeed/walkspeed.cpp
  roblox_external/features/waypoints/waypoints.cpp
  roblox_external/features/world/env.cpp
  roblox_external/features/world/world_esp.cpp
  roblox_external/offsets_live.cpp
  roblox_external/ui/menu_ui.cpp
  roblox_external/menu_linux.cpp
)

# ---- SDL2/GL detection (must run before we decide on backends) -----------
find_package(SDL2 QUIET)
find_package(OpenGL QUIET)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(SDL2_PC QUIET sdl2)
  if(SDL2_PC_FOUND AND NOT SDL2_FOUND)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_PC_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${SDL2_PC_LIBRARIES}"
      INTERFACE_COMPILE_OPTIONS "${SDL2_PC_CFLAGS_OTHER}")
    set(SDL2_FOUND TRUE)
    message(STATUS "SDL2 found via pkg-config: ${SDL2_PC_VERSION}")
  endif()
  pkg_check_modules(GL_PC QUIET gl)
  if(GL_PC_FOUND AND NOT OPENGL_FOUND)
    set(OPENGL_FOUND TRUE)
    message(STATUS "GL found via pkg-config")
  endif()
endif()
if(NOT SDL2_FOUND)
  find_program(SDL2_CONFIG sdl2-config)
  if(SDL2_CONFIG)
    execute_process(COMMAND ${SDL2_CONFIG} --cflags OUTPUT_VARIABLE SDL2_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${SDL2_CONFIG} --libs OUTPUT_VARIABLE SDL2_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(SDL2_CFLAGS AND SDL2_LIBS)
      add_library(SDL2::SDL2 INTERFACE IMPORTED)
      set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_COMPILE_OPTIONS "${SDL2_CFLAGS}"
        INTERFACE_LINK_LIBRARIES "${SDL2_LIBS}")
      set(SDL2_FOUND TRUE)
      message(STATUS "SDL2 found via sdl2-config")
    endif()
  endif()
endif()
if(NOT OPENGL_FOUND)
  find_path(OPENGL_INCLUDE_DIR GL/gl.h)
  if(OPENGL_INCLUDE_DIR)
    set(OPENGL_FOUND TRUE)
    message(STATUS "GL headers found at ${OPENGL_INCLUDE_DIR} — overlay will try GL")
  endif()
endif()

# ---- X11 / XFixes (P3 overlay control: game rect, click-through, hotkeys) --
# Not fatal when missing: x11overlay stubs compile everywhere and the windowed
# menu keeps working — only the transparent click-through overlay needs them.
find_package(X11 QUIET)
set(HAVE_X11 FALSE)
set(HAVE_XFIXES FALSE)
if(X11_FOUND)
  set(HAVE_X11 TRUE)
  message(STATUS "X11 found — EWMH game rect + overlay window control enabled")
  if(X11_Xfixes_LIB)
    set(HAVE_XFIXES TRUE)
    message(STATUS "XFixes found — click-through input shape enabled")
  else()
    message(STATUS "XFixes NOT found — click-through disabled (game rect still works)")
  endif()
else()
  message(STATUS "X11 not found — overlay control stubs only (windowed menu)")
endif()

add_executable(external
  ${IMGUI_SOURCES}
  ${GLASS_SOURCES}
  ${PLATFORM_SOURCES}
  ${CORE_SOURCES}
)

if(SDL2_FOUND AND OPENGL_FOUND)
  target_sources(external PRIVATE ${IMGUI_BACKEND_SOURCES})
  message(STATUS "SDL2+GL found — backends will be compiled")
endif()

target_include_directories(external PRIVATE
  roblox_external
  roblox_external/features
  "${GLASS_DIR}/include"
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/.."
)

target_compile_options(external PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_compile_definitions(external PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

if(HAVE_X11)
  target_compile_definitions(external PRIVATE HAS_X11=1)
  target_include_directories(external PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(external PRIVATE ${X11_X11_LIB})
  if(HAVE_XFIXES)
    target_compile_definitions(external PRIVATE HAS_XFIXES=1)
    target_link_libraries(external PRIVATE ${X11_Xfixes_LIB})
  endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(external PRIVATE Threads::Threads)

# (SDL2/GL detection already done above before add_executable)
if(SDL2_FOUND AND OPENGL_FOUND)
  if(TARGET SDL2::SDL2)
    target_link_libraries(external PRIVATE SDL2::SDL2)
  else()
    target_link_libraries(external PRIVATE ${SDL2_LIBRARIES})
  endif()
  if(OPENGL_FOUND AND TARGET OpenGL::GL)
    target_link_libraries(external PRIVATE OpenGL::GL)
  elseif(OPENGL_FOUND)
    target_link_libraries(external PRIVATE GL)
  endif()
  target_compile_definitions(external PRIVATE HAS_SDL_OVERLAY=1)
  message(STATUS "SDL2+GL found — overlay will be windowed")
else()
  message(STATUS "SDL2/GL not found — building headless (plug-and-play + scanner proven, overlay is log-only)")
  if(NOT SDL2_FOUND)
    message(STATUS "  hint: sudo xbps-install -y SDL2-devel pkg-config && pkg-config --modversion sdl2")
  endif()
  if(NOT OPENGL_FOUND)
    message(STATUS "  hint: sudo xbps-install -y mesa-devel libglvnd-devel")
  endif()
endif()

option(MUSL_STATIC "Link libstdc++/libgcc statically for musl Void" OFF)
if(MUSL_STATIC)
  target_link_options(external PRIVATE -static-libstdc++ -static-libgcc)
endif()

# ---- self-test helper (P1) ------------------------------------------------
add_executable(sober_probe EXCLUDE_FROM_ALL
  tools/sober_probe.cpp
  roblox_external/platform/platform_linux.cpp
  roblox_external/platform/sober_scanner.cpp
  roblox_external/platform/port_watcher.cpp
)
target_include_directories(sober_probe PRIVATE roblox_external)
target_link_libraries(sober_probe PRIVATE Threads::Threads)

# ---- probe with port_watcher ---------------------------------------------
add_executable(port_probe EXCLUDE_FROM_ALL
  tools/sober_probe.cpp
  roblox_external/platform/platform_linux.cpp
  roblox_external/platform/sober_scanner.cpp
  roblox_external/platform/port_watcher.cpp
)
target_include_directories(port_probe PRIVATE roblox_external)
target_link_libraries(port_probe PRIVATE Threads::Threads)

# ---- bootstrapper (dx9ware shape: watch Sober -> Load -> managed overlay) --
# The manager core (boot/bootstrap_core.cpp) is UI-free; the window talks to
# GTK3 through boot/gtk3_loader.cpp (dlopen — no GTK headers, no link
# dependency) and --console runs with no GTK at all. Load spawns 'external
# --locked'; the bootstrapper owns the overlay lifecycle from there.
# Roadmap lives in docs/BOOTSTRAPPER.md.
add_executable(roblox-external-boot
  boot/bootstrap_core.cpp
  boot/gtk3_loader.cpp
  boot/main_boot.cpp
  roblox_external/platform/platform_linux.cpp
  roblox_external/platform/sober_scanner.cpp
  roblox_external/platform/port_watcher.cpp
)
target_include_directories(roblox-external-boot PRIVATE roblox_external .)
target_link_libraries(roblox-external-boot PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(MUSL_STATIC)
  target_link_options(roblox-external-boot PRIVATE -static-libstdc++ -static-libgcc)
endif()

install(TARGETS external roblox-external-boot DESTINATION bin)
