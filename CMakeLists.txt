cmake_minimum_required(VERSION 3.18)
project(RenaAmp
    VERSION 0.1.0
    DESCRIPTION "Real-time Digital Guitar Amplifier Engine"
    LANGUAGES CXX
)

# ============================================================================
# OPTIONS
# ============================================================================
option(RENAAMP_BUILD_TESTS "Build test suite" OFF)
option(RENAAMP_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(RENAAMP_ENABLE_SANITIZERS "Enable AddressSanitizer and ThreadSanitizer" OFF)
option(RENAAMP_ENABLE_PROFILING "Enable profiling support" OFF)
option(RENAAMP_USE_PORTAUDIO "Use PortAudio instead of JACK" OFF)

# ============================================================================
# C++ STANDARD AND COMPILER FLAGS
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# BUILD CONFIGURATIONS
# ============================================================================
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -Wall -Wextra -Wpedantic")

# ============================================================================
# SANITIZERS
# ============================================================================
if(RENAAMP_ENABLE_SANITIZERS)
    message(STATUS "Sanitizers ENABLED")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=thread -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address -fsanitize=thread")
endif()

# ============================================================================
# PROFILING
# ============================================================================
if(RENAAMP_ENABLE_PROFILING)
    message(STATUS "Profiling ENABLED")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -pg")
endif()

# ============================================================================
# FIND DEPENDENCIES
# ============================================================================
find_package(Threads REQUIRED)

# Audio backend: JACK o PortAudio
if(RENAAMP_USE_PORTAUDIO)
    find_package(PortAudio REQUIRED)
    message(STATUS "Using PortAudio for audio I/O")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK REQUIRED jack)
    message(STATUS "Using JACK for audio I/O")

    # Homebrew path explícito para macOS (Apple Silicon)
    if(APPLE)
        find_library(JACK_LIB NAMES jack
            PATHS /opt/homebrew/lib /opt/homebrew/Cellar/jack/*/lib
            NO_DEFAULT_PATH
        )
        if(JACK_LIB)
            message(STATUS "Found JACK library: ${JACK_LIB}")
            set(JACK_LIBRARIES ${JACK_LIB})
        endif()
        # Include dirs de Homebrew
        if(EXISTS /opt/homebrew/include)
            list(APPEND JACK_INCLUDE_DIRS /opt/homebrew/include)
        endif()
    endif()
endif()

# nlohmann/json
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# RTNeural — Eigen backend
set(RTNEURAL_EIGEN ON CACHE BOOL "Use RTNeural with Eigen backend" FORCE)
set(RTNEURAL_DEFAULT_ALIGNMENT 16 CACHE STRING "RTNeural memory alignment" FORCE)
add_subdirectory(external/RTNeural)

# ============================================================================
# SOURCE FILES
# ============================================================================
set(CORE_SOURCES
    src/core/audio_engine.cpp
    src/core/audio_thread.cpp
    src/core/control_thread.cpp
    src/core/loader_thread.cpp
    src/core/watchdog_thread.cpp
)

set(CORE_HEADERS
    include/audio_engine.h
)

set(DSP_SOURCES
    src/dsp/gate.cpp
    src/dsp/saturation.cpp
    src/dsp/nam_processor.cpp
    src/dsp/cabinet_processor.cpp
    src/dsp/limiter.cpp
    src/dsp/dsp_chain.cpp
    src/dsp/rtneural_wrapper.cpp
)

set(THREADING_SOURCES
    src/threading/lock_free_queue.cpp
    src/threading/memory_pool.cpp
)

set(MODELS_SOURCES
    src/models/nam_model.cpp
    src/models/ir_model.cpp
    src/models/model_manager.cpp
)

set(NAM_CORE_SOURCES
    external/NeuralAmpModelerCore/NAM/activations.cpp
    external/NeuralAmpModelerCore/NAM/lstm.cpp
    external/NeuralAmpModelerCore/NAM/convnet.cpp
    external/NeuralAmpModelerCore/NAM/wavenet.cpp
    external/NeuralAmpModelerCore/NAM/dsp.cpp
    external/NeuralAmpModelerCore/NAM/get_dsp.cpp
    external/NeuralAmpModelerCore/NAM/util.cpp
)

set(PARAMETERS_SOURCES
    src/parameters/param_manager.cpp
    src/parameters/param_smoother.cpp
)

# NAM core siempre compila con excepciones (lo requiere internamente)
set_source_files_properties(${NAM_CORE_SOURCES} PROPERTIES
    COMPILE_FLAGS "-fexceptions"
)

set(RENAAMP_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${DSP_SOURCES}
    ${THREADING_SOURCES}
    ${MODELS_SOURCES}
    ${PARAMETERS_SOURCES}
    ${NAM_CORE_SOURCES}
)

# ============================================================================
# HELPER: include dirs compartidos
# ============================================================================
set(RENAAMP_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/RTNeural
    ${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore
    ${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore/Dependencies/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore/Dependencies/nlohmann
)

# ============================================================================
# MAIN EXECUTABLE
# ============================================================================
add_executable(renaamp ${RENAAMP_SOURCES} ${CORE_HEADERS})

target_include_directories(renaamp PRIVATE ${RENAAMP_INCLUDE_DIRS})

target_link_libraries(renaamp PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
    RTNeural
)

if(RENAAMP_USE_PORTAUDIO)
    target_link_libraries(renaamp PRIVATE PortAudio::PortAudio)
    target_compile_definitions(renaamp PRIVATE USE_PORTAUDIO)
else()
    target_link_libraries(renaamp PRIVATE ${JACK_LIBRARIES})
    target_include_directories(renaamp PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(renaamp PRIVATE USE_JACK)
endif()

target_compile_options(renaamp PRIVATE
    -fno-exceptions
    -fno-rtti
    -ffast-math
    -fPIC
)

# NAM core necesita excepciones dentro de renaamp también
set_source_files_properties(${NAM_CORE_SOURCES} PROPERTIES
    COMPILE_FLAGS "-fexceptions"
)

# ============================================================================
# LOAD_AND_RUN (test loader sin hardware)
# ============================================================================
set(LOAD_AND_RUN_SOURCES
    src/load_and_run.cpp
    ${DSP_SOURCES}
    ${MODELS_SOURCES}
    ${PARAMETERS_SOURCES}
    ${NAM_CORE_SOURCES}
)

add_executable(load_and_run ${LOAD_AND_RUN_SOURCES})

target_include_directories(load_and_run PRIVATE ${RENAAMP_INCLUDE_DIRS})

target_link_libraries(load_and_run PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
    RTNeural
    ${JACK_LIBRARIES}
)

target_include_directories(load_and_run PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(load_and_run PRIVATE USE_JACK)

target_compile_options(load_and_run PRIVATE
    -fno-exceptions
    -fno-rtti
    -ffast-math
    -fPIC
)

set_source_files_properties(${NAM_CORE_SOURCES} PROPERTIES
    COMPILE_FLAGS "-fexceptions"
)

# ============================================================================
# TESTS (Catch2)
# ============================================================================
if(RENAAMP_BUILD_TESTS)
    message(STATUS "Building tests")

    find_package(Catch2 QUIET)

    if(NOT Catch2_FOUND)
        # Desactivar autotests internos de Catch2 para no inflar el build
        set(CATCH_BUILD_TESTING    OFF CACHE BOOL "" FORCE)
        set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_EXTRA_TESTS OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.0
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    file(GLOB_RECURSE TEST_SOURCES
        "tests/unit/*.cpp"
        "tests/integration/*.cpp"
    )

    # ── FIX PRINCIPAL: incluir fuentes DSP que los tests referencian ──
    add_executable(renaamp_tests
        ${TEST_SOURCES}
        ${DSP_SOURCES}
        ${MODELS_SOURCES}
        ${PARAMETERS_SOURCES}
        ${NAM_CORE_SOURCES}
    )

    target_include_directories(renaamp_tests PRIVATE ${RENAAMP_INCLUDE_DIRS})

    target_link_libraries(renaamp_tests PRIVATE
        Catch2::Catch2WithMain
        Threads::Threads
        nlohmann_json::nlohmann_json
        RTNeural
        ${JACK_LIBRARIES}
    )

    target_include_directories(renaamp_tests PRIVATE ${JACK_INCLUDE_DIRS})

    # Define test fixtures directory path for tests to find fixture files
    target_compile_definitions(renaamp_tests PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/test_data"
    )

    # Tests necesitan excepciones (Catch2 las usa; NAM también)
    target_compile_options(renaamp_tests PRIVATE
        -fexceptions
        -ffast-math
        -fPIC
    )

    set_source_files_properties(${NAM_CORE_SOURCES} PROPERTIES
        COMPILE_FLAGS "-fexceptions"
    )

    enable_testing()
    include(CTest)
    include(Catch)
    catch_discover_tests(renaamp_tests)

    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS renaamp_tests
        COMMENT "Running all tests..."
    )
endif()

# ============================================================================
# BENCHMARKS
# ============================================================================
if(RENAAMP_BUILD_BENCHMARKS)
    message(STATUS "Building benchmarks")

    add_executable(renaamp_bench tests/benchmark/benchmark_main.cpp)

    target_link_libraries(renaamp_bench PRIVATE Threads::Threads)

    if(RENAAMP_USE_PORTAUDIO)
        target_link_libraries(renaamp_bench PRIVATE PortAudio::PortAudio)
    else()
        target_link_libraries(renaamp_bench PRIVATE ${JACK_LIBRARIES})
        target_include_directories(renaamp_bench PRIVATE ${JACK_INCLUDE_DIRS})
    endif()

    target_include_directories(renaamp_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endif()

# ============================================================================
# LEGACY TESTS (disabled - files moved/deprecated)
# ============================================================================
# test_nam_basic and test_dynamic_harness have been moved to docs/legacy/tests/
# Commented out as source files don't exist in current tree
# ============================================================================
# if(RENAAMP_BUILD_TESTS)
# add_executable(test_nam_basic
#     docs/legacy/tests/test_nam_basic.cpp
#     ${NAM_CORE_SOURCES}
# )
# ...
# endif()

# =========================================================================
# INSTALL
# ============================================================================
install(TARGETS renaamp RUNTIME DESTINATION bin)

install(DIRECTORY resources/
    DESTINATION share/renaamp/resources
    PATTERN ".gitkeep" EXCLUDE
)

# ============================================================================
# RESUMEN DE CONFIGURACIÓN
# ============================================================================
message(STATUS "")
message(STATUS "==================================")
message(STATUS "RenaAmp Configuration Summary")
message(STATUS "==================================")
message(STATUS "Version:          ${PROJECT_VERSION}")
message(STATUS "Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "Audio Backend:    ${RENAAMP_USE_PORTAUDIO}")
message(STATUS "Build Tests:      ${RENAAMP_BUILD_TESTS}")
message(STATUS "Build Benchmarks: ${RENAAMP_BUILD_BENCHMARKS}")
message(STATUS "Sanitizers:       ${RENAAMP_ENABLE_SANITIZERS}")
message(STATUS "Profiling:        ${RENAAMP_ENABLE_PROFILING}")
message(STATUS "RTNeural:         Enabled (Eigen backend)")
message(STATUS "==================================")
message(STATUS "")