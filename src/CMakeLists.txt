SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS "-Wall -fPIC -O2 -pthread")

STRING(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
ADD_DEFINITIONS("-DSOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}")

SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
# SET(CMAKE_EXE_LINKER_FLAGS "-static -pthread")
IF(CMAKE_BUILD_TYPE MATCHES DEBUG)
    SET(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-DDEBUG")
ENDIF(CMAKE_BUILD_TYPE MATCHES DEBUG)

INCLUDE_DIRECTORIES(.)

SET(COMP_FILES)
SET(COMP_FILES ${COMP_FILES}
               main.cc runtime.cc
               resmgr.cc options.cc 
               version.cc)
SET(COMP_FILES ${COMP_FILES}
               cli/cli.cc)
SET(COMP_FILES ${COMP_FILES}
               cpu/cpu.cc cpu/worker.cc 
               cpu/manager.cc cpu/manager_default.cc
               cpu/manager_random_normal.cc)
SET(COMP_FILES ${COMP_FILES}
               memory/memory.cc memory/manager.cc 
               memory/manager_default.cc)
SET(COMP_FILES ${COMP_FILES}
               util/log.cc util/normal_dist.cc
               util/proc_stat.cc)

ADD_EXECUTABLE(loadmaster ${COMP_FILES})
TARGET_LINK_LIBRARIES(loadmaster -static)
