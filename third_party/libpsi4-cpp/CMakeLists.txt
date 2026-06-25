# libpsi4-cpp: pure C++ Psi4 core library (no Python / no pybind exports).
# Source tree: ../../ref_psi4/psi4/src/psi4
cmake_minimum_required(VERSION 3.16)
project(libpsi4_cpp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Paths relative to this file: third_party/libpsi4-cpp/
set(RGPOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(PSI4_SRC_ROOT "${RGPOT_ROOT}/ref_psi4/psi4/src/psi4")
set(PSI4_INC_ROOT "${RGPOT_ROOT}/ref_psi4/psi4/include")

if(NOT EXISTS "${PSI4_SRC_ROOT}")
  message(FATAL_ERROR "Psi4 sources not found at ${PSI4_SRC_ROOT}. "
                      "Ensure ref_psi4/ is present (e.g. unzip ref_psi4.zip).")
endif()

option(LIBPSI4_CPP_INSTALL_HEADERS "Install/copy psi4 headers into prefix" ON)

# Collect C++ sources under psi4 modules; exclude Python/export/pybind glue.
file(GLOB_RECURSE PSI4_ALL_CC CONFIGURE_DEPENDS
  "${PSI4_SRC_ROOT}/*.cc"
)

set(PSI4_CORE_CC "")
foreach(src ${PSI4_ALL_CC})
  get_filename_component(fname "${src}" NAME)
  # Exclude pybind / python / export_* entrypoints (require embedded interpreter).
  if(fname MATCHES "^export_" OR fname MATCHES "pybind" OR fname MATCHES "python")
    continue()
  endif()
  # Exclude adcc / plugin loaders that pull Python heavily.
  if(src MATCHES "/export\\.cc$" OR src MATCHES "/core\\.cc$")
    # core.cc may be the pybind module; skip if it references PYBIND11_MODULE only path.
    # Keep non-export cc files in subdirs.
  endif()
  if(fname STREQUAL "core.cc" AND src MATCHES "/psi4/core\\.cc$")
    continue()
  endif()
  if(fname STREQUAL "export.cc")
    continue()
  endif()
  list(APPEND PSI4_CORE_CC "${src}")
endforeach()

list(LENGTH PSI4_CORE_CC _ncc)
message(STATUS "libpsi4-cpp: selected ${_ncc} C++ translation units from ref_psi4")

if(_ncc EQUAL 0)
  message(FATAL_ERROR "No Psi4 C++ sources selected; check PSI4_SRC_ROOT")
endif()

add_library(psi4 SHARED ${PSI4_CORE_CC})
add_library(libpsi4 ALIAS psi4)

target_include_directories(psi4
  PUBLIC
    $<BUILD_INTERFACE:${PSI4_INC_ROOT}>
    $<BUILD_INTERFACE:${PSI4_SRC_ROOT}/..>
    $<BUILD_INTERFACE:${PSI4_SRC_ROOT}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)

# Compile-only stubs so headers that include Python/pybind11 still parse.
target_include_directories(psi4 PRIVATE
  "${RGPOT_ROOT}/CppCore/rgpot/Psi4Pot/psi4_stubs"
)

target_compile_definitions(psi4 PRIVATE
  BUILDING_psi4=1
  USING_psi4=1
)

# Math libraries (BLAS/LAPACK/OpenMP) — best-effort; adjust for your toolchain.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(psi4 PUBLIC OpenMP::OpenMP_CXX)
endif()

find_package(LAPACK)
if(LAPACK_FOUND)
  target_link_libraries(psi4 PUBLIC ${LAPACK_LIBRARIES})
endif()

find_package(Threads REQUIRED)
target_link_libraries(psi4 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# Optional: Libint2, Libxc via pkg-config / cmake if present.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBXC IMPORTED_TARGET libxc)
  if(TARGET PkgConfig::LIBXC)
    target_link_libraries(psi4 PUBLIC PkgConfig::LIBXC)
    target_compile_definitions(psi4 PUBLIC USING_libxc=1)
  endif()
endif()

set_target_properties(psi4 PROPERTIES
  OUTPUT_NAME "psi4"
  VERSION "1.0.0"
  SOVERSION "1"
)

include(GNUInstallDirs)
install(TARGETS psi4
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(LIBPSI4_CPP_INSTALL_HEADERS)
  install(DIRECTORY "${PSI4_INC_ROOT}/psi4"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
  )
  # Also expose in-tree headers under src/psi4 for internal includes.
  install(DIRECTORY "${PSI4_SRC_ROOT}/"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/psi4/src/psi4
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
  )
endif()

# Convenience: config file for consumers
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/stubs/README.md"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/libpsi4-cpp
  OPTIONAL
)
