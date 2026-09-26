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
    src/legacy_config/legacy_config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/game_window.cpp
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

    # The frozen reader's value guards - headers only, nothing to link.
    wf2_add_test(config_sanitize)

    # Pure camera math, compiled straight from the production translation unit.
    wf2_add_test(camera_transform src/camera_transform.cpp)

    # Where the log and the config land. The mod calls cameraunlock::os for this
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

    # What the render path is allowed to write to the log. Every line there runs
    # once per camera update, so the edge-triggering and the caps are what keep
    # the log readable. Drives each latch through one designed sequence and reads
    # the file back. game_state comes along for ViewStateName, and the build
    # registry behind it.
    wf2_add_test(render_diagnostics src/render_diagnostics.cpp src/game_state.cpp
                src/builds/build_registry.cpp src/builds/steam_offsets.cpp)
    target_link_libraries(wf2_render_diagnostics_tests PRIVATE cameraunlock)

    # The committed config against the table's fresh render, the fleet's
    # default hotkeys, and the mode cycle's save. `wf2_config_tests
    # --render-config <path>` is pixi run render-config.
    wf2_add_test(config src/config.cpp src/legacy_config/legacy_config.cpp)
    target_link_libraries(wf2_config_tests PRIVATE cameraunlock)
    target_compile_definitions(wf2_config_tests PRIVATE
        WF2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the frozen loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    wf2_add_test(config_hotkeys src/legacy_config/legacy_config.cpp)
    target_link_libraries(wf2_config_hotkeys_tests PRIVATE cameraunlock)

    # And the rest of the frozen loader's boundary: which bool, float and port
    # values reach the Config, and which are refused and leave it as it was. A
    # refused value that looks like an unset one is the failure this locks out.
    wf2_add_test(config_values src/legacy_config/legacy_config.cpp)
    target_link_libraries(wf2_config_values_tests PRIVATE cameraunlock)

    # The canonical config conversion's differential test: v0.1.0's reader and
    # startup code (the oracle, compiled from byte copies in
    # tests/config_differential/oracle) against the frozen reader in
    # src/legacy_config/, and that against the config owner's migration into
    # CameraUnlock.ini.
    set(WF2_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(wf2_config_differential
        ${WF2_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${WF2_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(wf2_config_differential PRIVATE
        src
        ${WF2_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(wf2_config_differential PRIVATE
        WF2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(wf2_config_differential PRIVATE cameraunlock)
    set_target_properties(wf2_config_differential PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(wf2_config_differential PRIVATE /WX /EHsc /bigobj)
    endif()
    add_test(NAME config_differential COMMAND wf2_config_differential)
    set_property(GLOBAL APPEND PROPERTY WF2_TEST_TARGETS wf2_config_differential)
    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated/.
    add_test(NAME config_lint COMMAND node
             ${WF2_DIFFERENTIAL_DIR}/lint-migrated.mjs
             $<TARGET_FILE_DIR:wf2_config_differential>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.1.0's (git show v0.1.0:src/<file>),
    # the core sources both readers compile must stay the ones v0.1.0 was built
    # with (core 992b910), and the frozen reader is never edited.
    function(wf2_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(WF2_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${WF2_DIFFERENTIAL_DIR}/oracle/src/config.cpp|4d03d662bc25ca897a0d692eb11dc6fb0c1f1d5e79c9f09f0f2e22ec8bc23da4"
            "${WF2_DIFFERENTIAL_DIR}/oracle/src/config.h|cb1c7b9cf73e2dd3ed5713153f6d92fb835686af5d5c73ff90f27a74f9006aa2"
            "${WF2_DIFFERENTIAL_DIR}/oracle/src/config_sanitize.h|08487115c345a5bb851d191f1d157df617f2a6b8feea117209ea1b0d6d1abc54"
            "${WF2_DIFFERENTIAL_DIR}/oracle/src/logging.h|4eb3612099ac6df8eeeb4df47422e9c5b6f55e9aac21b5fc27665a0c3a5f3889"
            "${WF2_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${WF2_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${WF2_CORE_DIR}/include/cameraunlock/config/value_guards.h|6d3e3512bdfc9ac2a54bee750c5425191bdb4c24f3ae2ea3b9925612e01c78e5"
            "${WF2_CORE_DIR}/src/config/value_guards.cpp|a833ff7f2721f7974ce039a3597349a0974e6695ce3e75e598ce5bf4985a14b1"
            "${WF2_CORE_DIR}/include/cameraunlock/math/finite_utils.h|c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"
            "${WF2_CORE_DIR}/include/cameraunlock/protocol/port_utils.h|91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"
            "${WF2_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
            "${WF2_CORE_DIR}/include/cameraunlock/math/angle_utils.h|d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"
            "${WF2_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
            "${WF2_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${WF2_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|9be1463fcf023f1bc577e0a016b338d2c1e87692b64a91d7aee0d4746b1978ec"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|bddc75376b1e48d75c6cc86f5958196e97edb4a02acd0206891b7d2afaf9cb87"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/config_sanitize.h|1759afb5f42bb53e1c45c901a6577ba5b510274b21dddab4e0780099cb40408a")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        wf2_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()

    # Built from what wf2_add_test registered, so a new test cannot be added to
    # ctest and left out of the target run-tests.ps1 builds.
    get_property(wf2_test_targets GLOBAL PROPERTY WF2_TEST_TARGETS)
    add_custom_target(wf2_tests DEPENDS ${wf2_test_targets})
endif()
