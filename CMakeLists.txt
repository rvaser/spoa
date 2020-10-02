cmake_minimum_required(VERSION 3.9)

project(spoa VERSION 4.0.2
             LANGUAGES CXX
             DESCRIPTION "Spoa is a c++ library (and tool) for SIMD vectorized partial order alignment.")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build all libraries as shared")

option(spoa_optimize_for_native "Build spoa with -march=native" ON)
option(spoa_optimize_for_portability "Build spoa with -msse4.1" OFF)
option(spoa_use_simde "Use SIMDe library for porting vectorized code" OFF)
option(spoa_use_simde_nonvec "Use SIMDe library for nonvectorized code" OFF)
option(spoa_use_simde_openmp "Use SIMDe support for OpenMP SIMD" OFF)
option(spoa_generate_dispatch "Use SIMDe to generate x86 dispatch" OFF)
if (NOT spoa_generate_dispatch)
  if (spoa_optimize_for_portability)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.1")
  elseif (spoa_optimize_for_native)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
  endif ()
endif ()
if (spoa_use_simde OR
    spoa_use_simde_nonvec OR
    spoa_use_simde_openmp OR
    spoa_generate_dispatch)
  add_definitions(-DUSE_SIMDE -DSIMDE_ENABLE_NATIVE_ALIASES)
  if (spoa_use_simde_nonvec)
    add_definitions(-DSIMDE_NO_NATIVE)
  endif ()
  if (spoa_use_simde_openmp)
    add_definitions(-DSIMDE_ENABLE_OPENMP)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp-simd")
  endif ()
  if (spoa_generate_dispatch)
    add_definitions(-DGENERATE_DISPATCH)
  endif ()
endif ()

if (NOT TARGET cereal)
  add_subdirectory(vendor/cereal EXCLUDE_FROM_ALL)
endif ()

add_library(${PROJECT_NAME}
  src/alignment_engine.cpp
  src/graph.cpp
  src/sisd_alignment_engine.cpp
  src/dispatcher.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/simde>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/cpu_features/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(${PROJECT_NAME}
  cereal)
if (BUILD_SHARED_LIBS)
  set_property(TARGET ${PROJECT_NAME} PROPERTY SOVERSION = "5.0.0")
endif ()

if (spoa_generate_dispatch)
  if (NOT TARGET cpu_features)
    add_subdirectory(vendor/cpu_features EXCLUDE_FROM_ALL)
  endif ()

  list(APPEND ARCHITECTURES avx2 sse4.1 sse2)
  foreach(arch IN LISTS ARCHITECTURES)
    add_library(${PROJECT_NAME}_${arch} OBJECT
      src/simd_alignment_engine_dispatch.cpp)
    target_include_directories(${PROJECT_NAME}_${arch} PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/simde>
      $<INSTALL_INTERFACE:include>)
    set_target_properties(${PROJECT_NAME}_${arch} PROPERTIES
      COMPILE_FLAGS "-m${arch}")
    if (BUILD_SHARED_LIBS)
      set_property(TARGET ${PROJECT_NAME}_${arch}
        PROPERTY POSITION_INDEPENDENT_CODE ON)
    endif ()
  endforeach ()

  add_dependencies(${PROJECT_NAME}
    ${PROJECT_NAME}_avx2
    ${PROJECT_NAME}_sse4.1
    ${PROJECT_NAME}_sse2)

  target_link_libraries(${PROJECT_NAME}
    cpu_features)
endif ()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
  DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/spoa
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# configure and install pkg-config file
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/spoa.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/spoa-1.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/spoa-1.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

include_directories(${PROJECT_SOURCE_DIR}/vendor/cereal/include/)

option(spoa_build_executable "Build spoa standalone tool" OFF)
if (spoa_build_executable)
  if (NOT TARGET biosoup)
    add_subdirectory(vendor/bioparser/vendor/biosoup EXCLUDE_FROM_ALL)
  endif ()
  if (NOT TARGET bioparser)
    add_subdirectory(vendor/bioparser EXCLUDE_FROM_ALL)
  endif ()
  add_executable(${PROJECT_NAME}_exe
    src/main.cpp)
  target_link_libraries(${PROJECT_NAME}_exe
    ${PROJECT_NAME}
    bioparser
    biosoup)
  target_compile_definitions(${PROJECT_NAME}_exe PRIVATE
    SPOA_VERSION="v${PROJECT_VERSION}")
  set_property(TARGET ${PROJECT_NAME}_exe PROPERTY
    OUTPUT_NAME ${PROJECT_NAME})

  install(TARGETS ${PROJECT_NAME}_exe
    DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

option(spoa_build_tests "Build spoa unit tests" OFF)
if (spoa_build_tests)
  find_package(GTest REQUIRED)
  if (NOT TARGET biosoup)
    add_subdirectory(vendor/bioparser/vendor/biosoup EXCLUDE_FROM_ALL)
  endif ()
  if (NOT TARGET bioparser)
    add_subdirectory(vendor/bioparser EXCLUDE_FROM_ALL)
  endif ()
  include_directories(${PROJECT_SOURCE_DIR}/src)
  add_executable(${PROJECT_NAME}_test
    test/spoa_test.cpp)
  target_link_libraries(${PROJECT_NAME}_test
    ${PROJECT_NAME}
    bioparser
    biosoup
    GTest::Main)
  target_compile_definitions(${PROJECT_NAME}_test PRIVATE
    SPOA_DATA_PATH="${PROJECT_SOURCE_DIR}/test/data/sample.fastq.gz")
endif ()
