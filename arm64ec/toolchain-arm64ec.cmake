# CMake toolchain for cross-compiling Windows ARM64EC PE binaries on Linux
# using llvm-mingw. Point LLVM_MINGW_ROOT at the extracted llvm-mingw tree.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR arm64ec)

if(NOT DEFINED LLVM_MINGW_ROOT)
  set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()
if(NOT LLVM_MINGW_ROOT)
  message(FATAL_ERROR "Set LLVM_MINGW_ROOT to the llvm-mingw toolchain root")
endif()

set(TRIPLE arm64ec-w64-mingw32)

set(CMAKE_C_COMPILER   "${LLVM_MINGW_ROOT}/bin/${TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${TRIPLE}-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW_ROOT}/bin/${TRIPLE}-windres")
set(CMAKE_AR           "${LLVM_MINGW_ROOT}/bin/llvm-ar")
set(CMAKE_RANLIB       "${LLVM_MINGW_ROOT}/bin/llvm-ranlib")

# arm64ec is built as ARM64X into the aarch64 sysroot; there is no separate
# arm64ec-w64-mingw32 sysroot directory.
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/aarch64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
