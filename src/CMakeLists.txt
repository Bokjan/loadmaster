# Length of the absolute source-root prefix; used by the logger to print
# short file paths ("src/foo/bar.cc" instead of "/abs/path/src/foo/bar.cc").
string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)

find_package(Threads REQUIRED)

add_executable(loadmaster)

target_sources(loadmaster PRIVATE
    main.cc
    core/runtime.cc
    core/options.cc
    cli/cli.cc
    cli/version_string.cc
    cpu/stat.cc
    cpu/worker.cc
    cpu/manager.cc
    cpu/manager_default.cc
    cpu/manager_random_normal.cc
    cpu/factory.cc
    cpu/critical_loop.cc
    memory/factory.cc
    memory/manager.cc
    memory/manager_default.cc
    memory/allocator.cc
    util/log.cc
    util/normal_dist.cc
    util/proc_stat.cc
)

target_include_directories(loadmaster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(loadmaster PRIVATE cxx_std_20)

target_compile_definitions(loadmaster PRIVATE SOURCE_PATH_SIZE=${SOURCE_PATH_SIZE})

# Warnings: keep them strict, but don't error out on the few legacy bits the
# project still produces (let CI/devs see them and decide).
if(MSVC)
    target_compile_options(loadmaster PRIVATE /W4 /permissive-)
else()
    target_compile_options(loadmaster PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wno-unused-parameter
        -fPIC
    )
endif()

# Debug-only macro
target_compile_definitions(loadmaster PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Threading
target_link_libraries(loadmaster PRIVATE Threads::Threads)

# Optional fully-static linking: ON by default on Linux/GCC, off elsewhere.
option(LOADMASTER_STATIC_LINK "Statically link the loadmaster executable" ON)
if(LOADMASTER_STATIC_LINK AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(loadmaster PRIVATE -static)
endif()
