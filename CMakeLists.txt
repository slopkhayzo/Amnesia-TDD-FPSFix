cmake_minimum_required(VERSION 3.24)

project(
    AmnesiaTddFpsFix
    VERSION 1.0.0
    DESCRIPTION "High-frame-rate presentation fix for Amnesia: The Dark Descent"
    LANGUAGES C CXX
)

include(CTest)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "AmnesiaTDD.FPSFix currently supports 32-bit Windows/MSVC only")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure an x86 build environment; Amnesia.exe is a 32-bit process")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)
set_property(TARGET minhook PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# MODULE is the correct CMake model for a LoadLibrary-loaded ASI: unlike a
# normal SHARED library it does not require or expect a consumer import library.
add_library(AmnesiaTddFpsFix MODULE
    src/bootstrap.cpp
    src/camera_interpolation.cpp
    src/camera_runtime.cpp
    src/config.cpp
    src/dllmain.cpp
    src/enemy_pose_runtime.cpp
    src/frame_cadence.cpp
    src/hands_interpolation.cpp
    src/hands_runtime.cpp
    src/image_trail_timing.cpp
    src/logger.cpp
    src/motion_trace.cpp
    src/mouse_preview_math.cpp
    src/particle_interpolation.cpp
    src/presentation_clock.cpp
    src/pose_interpolation.cpp
    src/pose_runtime.cpp
    src/target_mapping.cpp
    src/telemetry.cpp
    src/world_runtime.cpp
)

target_compile_features(AmnesiaTddFpsFix PRIVATE cxx_std_20)
target_compile_definitions(AmnesiaTddFpsFix PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_compile_options(AmnesiaTddFpsFix PRIVATE
    /W4
    /WX
    /permissive-
    /utf-8
    /Zc:preprocessor
)
target_link_options(AmnesiaTddFpsFix PRIVATE
    /DYNAMICBASE
    /NXCOMPAT
    /GUARD:CF
    /INCREMENTAL:NO
)
target_link_libraries(AmnesiaTddFpsFix PRIVATE minhook)

set_property(TARGET AmnesiaTddFpsFix PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
set_target_properties(AmnesiaTddFpsFix PROPERTIES
    OUTPUT_NAME "AmnesiaTDD.FPSFix"
    PREFIX ""
    SUFFIX ".asi"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET AmnesiaTddFpsFix POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/AmnesiaTDD.FPSFix.ini"
            "$<TARGET_FILE_DIR:AmnesiaTddFpsFix>/AmnesiaTDD.FPSFix.ini"
    VERBATIM
)

install(TARGETS AmnesiaTddFpsFix
    LIBRARY DESTINATION .
    RUNTIME DESTINATION .
)
install(FILES config/AmnesiaTDD.FPSFix.ini DESTINATION .)

set(ATDD_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/package.cmake")
add_custom_target(package_plugin_only
    COMMAND "${CMAKE_COMMAND}"
            -DMODE=plugin-only
            -DPLUGIN_FILE=$<TARGET_FILE:AmnesiaTddFpsFix>
            -DINI_FILE=${CMAKE_SOURCE_DIR}/config/AmnesiaTDD.FPSFix.ini
            -DREADME_FILE=${CMAKE_SOURCE_DIR}/packaging/README.txt
            -DMINHOOK_LICENSE_FILE=${CMAKE_SOURCE_DIR}/third_party/minhook/LICENSE.txt
            -DOUTPUT_DIR=${CMAKE_SOURCE_DIR}/dist/release/AmnesiaTDD.FPSFix-${PROJECT_VERSION}-plugin-only
            -DARCHIVE_FILE=${CMAKE_SOURCE_DIR}/dist/AmnesiaTDD.FPSFix-${PROJECT_VERSION}-plugin-only.zip
            -P "${ATDD_PACKAGE_SCRIPT}"
    DEPENDS AmnesiaTddFpsFix
    VERBATIM
)

set(ATDD_UAL_X86 "${CMAKE_SOURCE_DIR}/research/third_party/dinput8.dll")
set(ATDD_UAL_NOTICE "${CMAKE_SOURCE_DIR}/research/third_party/THIRD-PARTY-NOTICES.txt")
if(EXISTS "${ATDD_UAL_X86}" AND EXISTS "${ATDD_UAL_NOTICE}")
    add_custom_target(package_ultimate_asi_loader
        COMMAND "${CMAKE_COMMAND}"
                -DMODE=ultimate-asi-loader
                -DPLUGIN_FILE=$<TARGET_FILE:AmnesiaTddFpsFix>
                -DINI_FILE=${CMAKE_SOURCE_DIR}/config/AmnesiaTDD.FPSFix.ini
                -DREADME_FILE=${CMAKE_SOURCE_DIR}/packaging/README.txt
                -DMINHOOK_LICENSE_FILE=${CMAKE_SOURCE_DIR}/third_party/minhook/LICENSE.txt
                -DLOADER_FILE=${ATDD_UAL_X86}
                -DNOTICE_FILE=${ATDD_UAL_NOTICE}
                -DOUTPUT_DIR=${CMAKE_SOURCE_DIR}/dist/release/AmnesiaTDD.FPSFix-${PROJECT_VERSION}-with-Ultimate-ASI-Loader
                -DARCHIVE_FILE=${CMAKE_SOURCE_DIR}/dist/AmnesiaTDD.FPSFix-${PROJECT_VERSION}-with-Ultimate-ASI-Loader.zip
                -P "${ATDD_PACKAGE_SCRIPT}"
        DEPENDS AmnesiaTddFpsFix
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_executable(frame_cadence_tests
        tests/frame_cadence_tests.cpp
        src/frame_cadence.cpp
    )
    target_include_directories(frame_cadence_tests PRIVATE src)
    target_compile_features(frame_cadence_tests PRIVATE cxx_std_20)
    target_compile_options(frame_cadence_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET frame_cadence_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME frame_cadence COMMAND frame_cadence_tests)

    add_executable(camera_interpolation_tests
        tests/camera_interpolation_tests.cpp
        src/camera_interpolation.cpp
    )
    target_include_directories(camera_interpolation_tests PRIVATE src)
    target_compile_features(camera_interpolation_tests PRIVATE cxx_std_20)
    target_compile_options(camera_interpolation_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET camera_interpolation_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME camera_interpolation COMMAND camera_interpolation_tests)

    add_executable(camera_runtime_tests
        tests/camera_runtime_tests.cpp
        src/camera_interpolation.cpp
        src/camera_runtime.cpp
    )
    target_include_directories(camera_runtime_tests PRIVATE src)
    target_compile_features(camera_runtime_tests PRIVATE cxx_std_20)
    target_compile_definitions(camera_runtime_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(camera_runtime_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET camera_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME camera_runtime COMMAND camera_runtime_tests)

    add_executable(hands_interpolation_tests
        tests/hands_interpolation_tests.cpp
        src/hands_interpolation.cpp
    )
    target_include_directories(hands_interpolation_tests PRIVATE src)
    target_compile_features(hands_interpolation_tests PRIVATE cxx_std_20)
    target_compile_options(hands_interpolation_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET hands_interpolation_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME hands_interpolation COMMAND hands_interpolation_tests)

    add_executable(hands_runtime_tests
        tests/hands_runtime_tests.cpp
        src/hands_interpolation.cpp
        src/hands_runtime.cpp
    )
    target_include_directories(hands_runtime_tests PRIVATE src)
    target_compile_features(hands_runtime_tests PRIVATE cxx_std_20)
    target_compile_definitions(hands_runtime_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(hands_runtime_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET hands_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME hands_runtime COMMAND hands_runtime_tests)

    add_executable(pose_interpolation_tests
        tests/pose_interpolation_tests.cpp
        src/hands_interpolation.cpp
        src/pose_interpolation.cpp
    )
    target_include_directories(pose_interpolation_tests PRIVATE src)
    target_compile_features(pose_interpolation_tests PRIVATE cxx_std_20)
    target_compile_options(pose_interpolation_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET pose_interpolation_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME pose_interpolation COMMAND pose_interpolation_tests)

    add_executable(pose_runtime_tests
        tests/pose_runtime_tests.cpp
        src/hands_interpolation.cpp
        src/pose_interpolation.cpp
        src/pose_runtime.cpp
    )
    target_include_directories(pose_runtime_tests PRIVATE src)
    target_compile_features(pose_runtime_tests PRIVATE cxx_std_20)
    target_compile_definitions(pose_runtime_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(pose_runtime_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET pose_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME pose_runtime COMMAND pose_runtime_tests)

    add_executable(enemy_pose_runtime_tests
        tests/enemy_pose_runtime_tests.cpp
        src/enemy_pose_runtime.cpp
        src/hands_interpolation.cpp
        src/pose_interpolation.cpp
    )
    target_include_directories(enemy_pose_runtime_tests PRIVATE src)
    target_compile_features(enemy_pose_runtime_tests PRIVATE cxx_std_20)
    target_compile_definitions(enemy_pose_runtime_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(enemy_pose_runtime_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET enemy_pose_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME enemy_pose_runtime COMMAND enemy_pose_runtime_tests)

    add_executable(world_runtime_tests
        tests/world_runtime_tests.cpp
        src/hands_interpolation.cpp
        src/world_runtime.cpp
    )
    target_include_directories(world_runtime_tests PRIVATE src)
    target_compile_features(world_runtime_tests PRIVATE cxx_std_20)
    target_compile_definitions(world_runtime_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(world_runtime_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET world_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME world_runtime COMMAND world_runtime_tests)

    add_executable(presentation_clock_tests
        tests/presentation_clock_pair_tests.cpp
        src/presentation_clock.cpp
    )
    target_include_directories(presentation_clock_tests PRIVATE src)
    target_compile_features(presentation_clock_tests PRIVATE cxx_std_20)
    target_compile_options(presentation_clock_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET presentation_clock_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME presentation_clock COMMAND presentation_clock_tests)

    add_executable(mouse_preview_math_tests
        tests/mouse_preview_math_tests.cpp
        src/mouse_preview_math.cpp
    )
    target_include_directories(mouse_preview_math_tests PRIVATE src)
    target_compile_features(mouse_preview_math_tests PRIVATE cxx_std_20)
    target_compile_options(mouse_preview_math_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET mouse_preview_math_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME mouse_preview_math COMMAND mouse_preview_math_tests)

    add_executable(particle_interpolation_tests
        tests/particle_interpolation_tests.cpp
        src/particle_interpolation.cpp
    )
    target_include_directories(particle_interpolation_tests PRIVATE src)
    target_compile_features(particle_interpolation_tests PRIVATE cxx_std_20)
    target_compile_options(particle_interpolation_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET particle_interpolation_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME particle_interpolation COMMAND particle_interpolation_tests)

    add_executable(image_trail_timing_tests
        tests/image_trail_timing_tests.cpp
        src/image_trail_timing.cpp
    )
    target_include_directories(image_trail_timing_tests PRIVATE src)
    target_compile_features(image_trail_timing_tests PRIVATE cxx_std_20)
    target_compile_options(image_trail_timing_tests PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET image_trail_timing_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME image_trail_timing COMMAND image_trail_timing_tests)

    add_executable(asi_bootstrap_host tests/asi_bootstrap_host.cpp)
    target_compile_features(asi_bootstrap_host PRIVATE cxx_std_20)
    target_compile_definitions(asi_bootstrap_host PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
    target_compile_options(asi_bootstrap_host PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
    )
    set_property(TARGET asi_bootstrap_host PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    set_target_properties(asi_bootstrap_host PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(
        NAME asi_bootstrap_rejects_incompatible_host
        COMMAND asi_bootstrap_host "$<TARGET_FILE:AmnesiaTddFpsFix>"
    )

    find_program(POWERSHELL_EXECUTABLE NAMES powershell.exe powershell REQUIRED)
    add_test(
        NAME release_validation_analyzer
        COMMAND "${POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/research/validation/analyze-run.ps1"
                -PresentMonCsv "${CMAKE_SOURCE_DIR}/tests/fixtures/release-validation-presentmon.csv"
                -PluginLog "${CMAKE_SOURCE_DIR}/tests/fixtures/release-validation-plugin.log"
                -TargetHz 120
                -Scenario steady
    )
    add_test(
        NAME release_validation_telemetry_analyzer
        COMMAND "${POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/research/validation/analyze-telemetry-run.ps1"
                -PluginLog "${CMAKE_SOURCE_DIR}/tests/fixtures/release-validation-plugin.log"
                -TargetHz 120
                -Scenario steady
    )
endif()
