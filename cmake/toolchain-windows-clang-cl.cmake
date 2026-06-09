# Cross-compile the TEKK Preamp for Windows (x86_64) from Linux with clang-cl.
#
# JUCE 8 dropped MinGW support (juce_TargetPlatform.h: "Support for MinGW has
# been removed"), so the only working cross-compiler is clang-cl, which targets
# the MSVC ABI and which JUCE treats as MSVC. That needs the real MSVC CRT +
# Windows SDK, fetched with xwin (https://github.com/Jake-Shadle/xwin):
#
#   sudo apt install clang lld llvm
#   cargo install xwin   # or grab a release binary
#   xwin --accept-license splat --output /opt/xwin     # downloads ~1.5 GB from MS
#
#   cd plugin
#   cmake -B build-win -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=$(pwd)/../cmake/toolchain-windows-clang-cl.cmake \
#         -DXWIN_DIR=/opt/xwin
#   cmake --build build-win
#
# JUCE bootstraps a *host* juceaide automatically (it can't run the Windows one).
# NOTE: xwin's default "splat" uses gnu-style arch dirs (x86_64). If you pass
# --preserve-ms-arch-notation, change x86_64 -> x64 below.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_AR           llvm-lib)
set(CMAKE_MT           llvm-mt)

if(NOT DEFINED XWIN_DIR)
    set(XWIN_DIR /opt/xwin CACHE PATH "Root of the xwin-splatted MSVC CRT + Windows SDK")
endif()

set(_triple --target=x86_64-pc-windows-msvc)
set(_inc "/imsvc${XWIN_DIR}/crt/include \
/imsvc${XWIN_DIR}/sdk/include/ucrt \
/imsvc${XWIN_DIR}/sdk/include/um \
/imsvc${XWIN_DIR}/sdk/include/shared")

set(CMAKE_C_FLAGS_INIT   "${_triple} ${_inc} -Wno-unused-command-line-argument")
set(CMAKE_CXX_FLAGS_INIT "${_triple} ${_inc} -Wno-unused-command-line-argument")

set(_libs "/libpath:${XWIN_DIR}/crt/lib/x86_64 \
/libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64 \
/libpath:${XWIN_DIR}/sdk/lib/um/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libs}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # host tools (cmake, host juceaide)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
