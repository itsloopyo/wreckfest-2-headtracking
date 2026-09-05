cmake_minimum_required(VERSION 3.20)
project(Wreckfest2HeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

# MinHook is vendored rather than fetched: `pixi run package` has to build from
# a clean checkout with no network, and the source compiled into the binary
# users install should be the source in this repo.
add_library(minhook STATIC
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /W0)
endif()

# HOOKS carries hooks/hook_manager.cpp, the MinHook wrapper the camera hook
# places its detour through. DISCOVERY stays off: this mod reaches the camera
# through the RVA its build profile pins, not by RTTI vtable resolution.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_DISCOVERY OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/game_window.cpp
    src/hotkey_names.cpp
    src/hotkeys.cpp
    src/render_diagnostics.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(Wreckfest2HeadTracking SHARED ${MOD_SOURCES})

# The same /WX the tests carry. The three files that appear in no test target -
# dllmain, headtracking_mod and camera_hook - are the ones doing reinterpret_cast
# pointer arithmetic and raw memcpy on the render path, which is where a
# truncation or sign-conversion warning is worth stopping the build for. The
# tree builds warning-free today, so this costs nothing to turn on.
if(MSVC)
    target_compile_options(Wreckfest2HeadTracking PRIVATE /WX)
endif()

target_include_directories(Wreckfest2HeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(Wreckfest2HeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
    psapi
)

# Emit a linker map so a crash offset (asi+0xNNNN) resolves to a symbol.
if(MSVC)
    target_link_options(Wreckfest2HeadTracking PRIVATE /MAP /MAPINFO:EXPORTS)
endif()

# Output as .asi for Ultimate ASI Loader.
set_target_properties(Wreckfest2HeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Opt-in unit tests. OFF by default so the normal build and the release
# packager never produce a test binary next to the .asi. Enabled by
# scripts/run-tests.ps1 (`pixi run test`), which builds the wf2_tests target.
option(WF2_BUILD_TESTS "Build Wreckfest2HeadTracking unit tests" OFF)
if(WF2_BUILD_TESTS)
    enable_testing()

    function(wf2_add_test test_name)
        set(target "wf2_${test_name}_tests")
        add_executable(${target} tests/${test_name}_tests.cpp ${ARGN})
        target_include_directories(${target} PRIVATE
            src
            cameraunlock-core/cpp/include
        )
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        # /W4 and /permissive- are already on globally; tests additionally
        # treat warnings as errors.
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        add_test(NAME ${test_name} COMMAND ${target})
        set_property(GLOBAL APPEND PROPERTY WF2_TEST_TARGETS ${target})
    endfunction()

    # Pure boundary logic - headers only, nothing to link.
    wf2_add_test(config_sanitize)

    # Pure camera math, compiled straight from the production translation unit.
    wf2_add_test(camera_transform src/camera_transform.cpp)

    # Where the log and the INI land. The mod calls cameraunlock::os for this
    # rather than carrying its own copy, so this locks the contract the
    # bootstrap depends on. The running test EXE stands in for the game.
    wf2_add_test(exe_paths)
    target_link_libraries(wf2_exe_paths_tests PRIVATE cameraunlock)

    # The gameplay gate's classification, compiled from the production
    # translation unit. Reading the engine's state needs the game; deciding
    # what each state means for tracking does not. The build registry comes
    # along because InitGameState resolves its offsets from the active profile.
    wf2_add_test(game_state src/game_state.cpp
                src/builds/build_registry.cpp src/builds/steam_offsets.cpp)
    target_link_libraries(wf2_game_state_tests PRIVATE cameraunlock)

    # The per-build offset registry. Append-only is a policy the compiler cannot
    # enforce, so the shipped profile's fingerprint and every offset the render
    # path uses are pinned here against .lab/NOTES.md, along with the newest-
    # first ordering the "unknown build" diagnostic depends on.
    wf2_add_test(build_profiles src/builds/build_registry.cpp src/builds/steam_offsets.cpp)
    target_link_libraries(wf2_build_profiles_tests PRIVATE cameraunlock)

    # Where a centred window starts. The rest of the centring is Win32 calls
    # that need a desktop and a running game; the arithmetic that decides the
    # position - including the clamp that keeps an oversized window's title bar
    # on screen - does not.
    wf2_add_test(game_window src/game_window.cpp)
    target_link_libraries(wf2_game_window_tests PRIVATE cameraunlock)

    # The one log line that reports which keys the config landed on. What a key
    # is called is the keyboard layout's business; that a name is always
    # produced is this mod's.
    wf2_add_test(hotkey_names src/hotkey_names.cpp)

    # The sentence that line is built from: which action ended up on which pair
    # of keys. Registering the bindings needs the poller thread; describing them
    # is pure.
    wf2_add_test(hotkeys src/hotkeys.cpp src/hotkey_names.cpp)
    target_link_libraries(wf2_hotkeys_tests PRIVATE cameraunlock)

    # What the render path is allowed to write to the log. Every line there runs
    # once per camera update, so the edge-triggering and the caps are what keep
    # the log readable. Drives each latch through one designed sequence and reads
    # the file back. game_state comes along for ViewStateName, and the build
    # registry behind it.
    wf2_add_test(render_diagnostics src/render_diagnostics.cpp src/game_state.cpp
                src/builds/build_registry.cpp src/builds/steam_offsets.cpp)
    target_link_libraries(wf2_render_diagnostics_tests PRIVATE cameraunlock)

    # Exercises the real INI generator and loader, so it needs the core's
    # IniReader and the log sink config.cpp writes its diagnostics to.
    wf2_add_test(config_defaults src/config.cpp)
    target_link_libraries(wf2_config_defaults_tests PRIVATE cameraunlock)
    target_compile_definitions(wf2_config_defaults_tests PRIVATE
        WF2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the same loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    wf2_add_test(config_hotkeys src/config.cpp)
    target_link_libraries(wf2_config_hotkeys_tests PRIVATE cameraunlock)

    # And the rest of the loader's boundary: which bool, float and port values
    # reach the Config, and which are refused and leave it as it was. A refused
    # value that looks like an unset one is the failure this locks out.
    wf2_add_test(config_values src/config.cpp)
    target_link_libraries(wf2_config_values_tests PRIVATE cameraunlock)

    # Built from what wf2_add_test registered, so a new test cannot be added to
    # ctest and left out of the target run-tests.ps1 builds.
    get_property(wf2_test_targets GLOBAL PROPERTY WF2_TEST_TARGETS)
    add_custom_target(wf2_tests DEPENDS ${wf2_test_targets})
endif()
