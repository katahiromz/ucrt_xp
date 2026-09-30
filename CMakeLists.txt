cmake_minimum_required(VERSION 3.10)
project(ucrt_xp C CXX)

# --- XP targeting -----------------------------------------------------
# Forces the "禁止シンボル" (Vista+-only API) door shut at the toolchain
# level: nothing newer than XP can even resolve at link time against the
# import libs selected by these defines/subsystem version.
add_definitions(-D_WIN32_WINNT=0x0501 -DWINVER=0x0501)

if (MSVC)
    # Tells the linker to require an XP-compatible OS subsystem version.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS,5.01")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SUBSYSTEM:WINDOWS,5.01")
endif()

set(UCRT_XP_SOURCES
    src/init.c
    src/once.c
    src/condvar.c
    src/sync.c
    src/heap.c
    src/locale.c
    src/exception.c
    src/stdio.c
    src/format.c
    src/scan.c
    src/wide.c
    src/string.c
    src/ctype.c
    src/convert.c
    src/process.c
    src/time.c
)

include_directories(include)

# --- Shared DLL (the default, recommended deployment: app-local copy) -
add_library(ucrt_xp SHARED ${UCRT_XP_SOURCES} src/ucrt_xp.def)
target_compile_definitions(ucrt_xp PRIVATE UCRT_XP_BUILD_DLL)
set_target_properties(ucrt_xp PROPERTIES PREFIX "")

# --- Static subset (single-exe / offline deployment) -------------------
# Same sources, no dllexport plumbing needed since UCRT_XP_BUILD_DLL is
# not defined; the __declspec(dllexport) tags in the public header only
# apply when building the DLL. For the static target we still compile
# the same .c files, but they end up as ordinary linkable symbols.
add_library(ucrt_xp_static STATIC ${UCRT_XP_SOURCES})
set_target_properties(ucrt_xp_static PROPERTIES OUTPUT_NAME "ucrt_xp_static")

# --- Examples -------------------------------------------------------
add_executable(hello_example examples/hello.c)
target_link_libraries(hello_example ucrt_xp_static)

add_executable(hello_cpp_example examples/hello.cpp)
target_link_libraries(hello_cpp_example ucrt_xp_static)

# --- Tests ---------------------------------------------------------
enable_testing()
add_executable(ucrt_xp_tests tests/test_basic.c)
target_link_libraries(ucrt_xp_tests ucrt_xp_static)
add_test(NAME basic COMMAND ucrt_xp_tests)

add_executable(ucrt_xp_sync_tests tests/test_sync.c)
target_link_libraries(ucrt_xp_sync_tests ucrt_xp_static)
add_test(NAME sync COMMAND ucrt_xp_sync_tests)
