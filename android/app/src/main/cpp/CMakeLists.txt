cmake_minimum_required(VERSION 3.22)
project(NFSMWAndroidBootstrap C CXX)
if(NOT ANDROID OR NOT (ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "armeabi-v7a"))
    message(FATAL_ERROR "This target requires Android arm64-v8a or armeabi-v7a")
endif()
# libadrenotools (custom Vulkan drivers) exists for arm64 only.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(NFS_CUSTOM_DRIVERS ON)
else()
    set(NFS_CUSTOM_DRIVERS OFF)
endif()
# -PnfsCpu=<cpu> (gradle) tunes every native target, DXVK included, for one
# SoC: e.g. cortex-a720 for the Dimensity 8400 (Xiaomi 15T). Such a build does
# not run on older cores. Empty = the portable ARMv8-A baseline. SVE/SVE2 stay
# off: kernels commonly leave them disabled for userspace even on cores that
# implement them (SIGILL at launch in the NFSU2 port).
set(NFS_GUEST_CPU "" CACHE STRING "clang -mcpu value for all native code (empty = baseline)")
if(NFS_GUEST_CPU)
    add_compile_options("-mcpu=${NFS_GUEST_CPU}+nosve+nosve2+nosve2-bitperm")
endif()
add_library(native_app_glue STATIC "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${ANDROID_NDK}/sources/android/native_app_glue")
get_filename_component(NFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(PCRECOMP "${NFS_ROOT}/../pcrecomp")
# Shared with the NFSU2 port: its prepared DXVK tree and pinned libadrenotools (sibling checkout).
set(NFSU2_ROOT "${NFS_ROOT}/../nfsu2-recompiled" CACHE PATH "nfsu2-recompiled checkout")
set(DXVK_SOURCE "${NFSU2_ROOT}/build/dxvk-source" CACHE PATH "Prepared DXVK native source")
set(DXVK_ANDROID_STATIC ON CACHE BOOL "Link into application" FORCE)
add_subdirectory("${NFSU2_ROOT}/ports/dxvk_android" "${CMAKE_CURRENT_BINARY_DIR}/dxvk")
set(GEN_DIR "${NFS_ROOT}/src/recomp/gen")
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${GEN_DIR}/recomp_*.c")
if(NOT GEN_SOURCES)
    message(FATAL_ERROR "Generate the local game C first; see the project README")
endif()
# Very large translation units leave one compiler working long after all other
# jobs finish. Split only those units, preserving complete functions and headers.
set(ANDROID_GEN_SOURCES)
foreach(source IN LISTS GEN_SOURCES)
    file(SIZE "${source}" source_bytes)
    if(source_bytes GREATER 67108864)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" "${NFS_ROOT}/scripts/split_android_sources.py")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${NFS_ROOT}/scripts/split_android_sources.py"
            "${source}" "${CMAKE_CURRENT_BINARY_DIR}/split_guest"
            RESULT_VARIABLE split_status OUTPUT_VARIABLE split_sources OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT split_status EQUAL 0)
            message(FATAL_ERROR "Failed to split lifted Android source: ${source}")
        endif()
        string(REPLACE "\n" ";" split_sources "${split_sources}")
        list(APPEND ANDROID_GEN_SOURCES ${split_sources})
    else()
        list(APPEND ANDROID_GEN_SOURCES "${source}")
    endif()
endforeach()
# server.dll (EA's LAN game server) lifted from a copy relocated to its guest
# base: scripts/prepare_module.py + run_lift.py --prefix server_. LAN hosting
# requires it; do not silently produce an APK with a missing server module.
set(SERVER_GEN_DIR "${NFS_ROOT}/src/recomp/gen_server")
file(GLOB SERVER_GEN_SOURCES CONFIGURE_DEPENDS "${SERVER_GEN_DIR}/server_recomp_*.c")
if(NOT SERVER_GEN_SOURCES)
    message(FATAL_ERROR "Missing LAN server code: run scripts/prepare_server.py with your server.dll")
endif()
list(APPEND ANDROID_GEN_SOURCES ${SERVER_GEN_SOURCES})
# x86 routines of EA's sound banks, lifted from the local Sound/**.abk
# (scripts/abk_module.py + run_lift.py --prefix abk_; see src/runtime/nfs_abk.h).
# d3dx9_26.dll (DirectX runtime) lifted as a module: speed.exe imports its
# effect framework and math, and Android has no D3DX.
set(D3DX_GEN_DIR "${NFS_ROOT}/src/recomp/gen_d3dx")
file(GLOB D3DX_GEN_SOURCES CONFIGURE_DEPENDS "${D3DX_GEN_DIR}/d3dx_recomp_*.c")
if(NOT D3DX_GEN_SOURCES)
    message(FATAL_ERROR "No d3dx9_26 code in ${D3DX_GEN_DIR}: prepare_module.py + run_lift.py --seeds-only/--prefix d3dx_")
endif()
list(APPEND ANDROID_GEN_SOURCES ${D3DX_GEN_SOURCES})
set(ABK_GEN_DIR "${NFS_ROOT}/src/recomp/gen_abk")
file(GLOB ABK_GEN_SOURCES CONFIGURE_DEPENDS "${ABK_GEN_DIR}/abk_*.c")
if(NOT ABK_GEN_SOURCES)
    message(FATAL_ERROR "No sound-bank code in ${ABK_GEN_DIR}: run scripts/abk_module.py and run_lift.py --prefix abk_")
endif()
list(APPEND ANDROID_GEN_SOURCES ${ABK_GEN_SOURCES})
set_property(GLOBAL PROPERTY JOB_POOLS guest_compile=4)
add_library(nfs_guest OBJECT ${ANDROID_GEN_SOURCES})
set_property(TARGET nfs_guest PROPERTY JOB_POOL_COMPILE guest_compile)
target_include_directories(nfs_guest PRIVATE "${PCRECOMP}/runtime/recomp32" "${GEN_DIR}" "${SERVER_GEN_DIR}" "${NFS_ROOT}/src/runtime")
# RECOMP_LOCAL_REGS: guest registers live in locals of each lifted function
# (written back at every call and return) so they stay in ARM64 registers.
# RECOMP_MEM_QUAL=: guest memory accesses are not volatile; -fno-strict-aliasing
# keeps the untyped guest memory correct, and lifted loops still make an opaque
# call per back-edge. -fwrapv: x86 integer arithmetic wraps. Both validated by
# scripts/difftest_real.py --cc clang-o2 --local-regs (same result as MSVC).
# RECOMP_X87_WINDOW: the x87 stack is a sliding window (push/pop move one
# index instead of shifting 16 values). Every unit of the library must agree,
# so nfsmw_android (runtime, overrides, widescreen) defines it too.
target_compile_definitions(nfs_guest PRIVATE RECOMP_STRICT_ICALL RECOMP_LOCAL_REGS RECOMP_X87_WINDOW "RECOMP_MEM_QUAL=")
# Optimize the ARM64 game while avoiding aliasing assumptions in x86 accesses;
# retain unwind tables so unsupported HLE paths can exit to the host cleanly.
# NFS_GUEST_OPT: -O2 (default) or -O3 for the lifted game code; gradle -PnfsOpt=O3.
set(NFS_GUEST_OPT "O2" CACHE STRING "Optimisation level of the lifted game code (O2 or O3)")
target_compile_options(nfs_guest PRIVATE -${NFS_GUEST_OPT} -w -fwrapv -fno-strict-aliasing -fexceptions -ffp-contract=off
    -include "${CMAKE_CURRENT_SOURCE_DIR}/arm64_guest.h")
# The ~27,700 lifted functions are internal to the library: hidden visibility
# turns their cross-file calls from PLT stubs into direct calls, and ThinLTO
# (as in the MW port) can inline small game functions across the generated
# files, which also removes their guest register save/reload. No debug info:
# it only slows the build of 9M generated lines.
option(NFS_GUEST_LTO "ThinLTO over the lifted game code" ON)
target_compile_options(nfs_guest PRIVATE -g0 -fvisibility=hidden)
# ARMv7: guest data is only as aligned as x86 needed, but the lifted code reads
# floats and doubles through plain pointers, and the compiler would use
# LDRD/STRD/VLDR, which fault on such addresses (SIGBUS BUS_ADRALN). ARM64
# loads tolerate it; on 32-bit assume no alignment for unannotated pointers.
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(nfs_guest PRIVATE -fmax-type-align=1)
endif()
if(NFS_GUEST_LTO)
    target_compile_options(nfs_guest PRIVATE -flto=thin)
endif()
# Custom Vulkan drivers (Turnip, ...) imported in the launcher, as in the MW/Carbon ports.
# libadrenotools is pinned in third_party/ (BSD-2-Clause); no driver is bundled.
if(NFS_CUSTOM_DRIVERS)
    add_subdirectory("${NFSU2_ROOT}/third_party/libadrenotools" "${CMAKE_CURRENT_BINARY_DIR}/adrenotools")
endif()
add_library(nfsmw_diagnostics SHARED gpu_probe.cpp vulkan_driver.cpp)
target_link_libraries(nfsmw_diagnostics PRIVATE android log dl)
target_compile_options(nfsmw_diagnostics PRIVATE -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers)
add_library(nfsmw_android SHARED main.cpp guest_runtime.cpp d3d9_bridge.cpp vulkan_driver.cpp "${NFS_ROOT}/src/runtime/overrides.c" "${NFS_ROOT}/src/runtime/widescreen.c" "${NFS_ROOT}/src/runtime/abk_code.c" $<TARGET_OBJECTS:nfs_guest>)
target_include_directories(nfsmw_android SYSTEM PRIVATE "${DXVK_SOURCE}/include/native"
  "${DXVK_SOURCE}/include/native/windows" "${DXVK_SOURCE}/include/native/directx")
target_include_directories(nfsmw_android PRIVATE "${PCRECOMP}/runtime/recomp32" "${PCRECOMP}/runtime/win32hle" "${NFS_ROOT}/src/runtime" "${NFSU2_ROOT}/src/runtime")
target_compile_definitions(nfsmw_android PRIVATE RECOMP_STRICT_ICALL RECOMP_X87_WINDOW)
if(SERVER_GEN_SOURCES)
    target_compile_definitions(nfsmw_android PRIVATE NFS_SERVER_DLL)
endif()
target_compile_features(nfsmw_android PRIVATE cxx_std_17)
target_compile_definitions(nfsmw_android PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
# Hidden by default (NFSU2 port): the lifted code reaches recomp_lookup*, the
# import bridges and the override table on every indirect call, and default
# visibility sent each hop through the PLT. JNIEXPORT keeps the Java entry
# points exported; ANativeActivity_onCreate comes from native_app_glue (-u).
target_compile_options(nfsmw_android PRIVATE -O2 -fvisibility=hidden -fno-strict-aliasing -ffp-contract=off -Wall -Wextra -Werror -Wno-missing-field-initializers)
target_link_options(nfsmw_android PRIVATE "-uANativeActivity_onCreate")
# -PnfsFast (gradle): guest memory at a fixed host address so the lifted code
# adds an immediate, and no frame pointers. The 4 GB reservation at
# 0x100000000 needs a 64-bit process; see arm64_guest.h and guest_memory.h.
# Lifted loop yields stay unless -PnfsNoYield (NFS_GUEST_NO_YIELD) as well.
option(NFS_GUEST_FAST "Fixed guest base, no frame pointers" OFF)
option(NFS_GUEST_NO_YIELD "No scheduler yield counter in lifted loops" OFF)
if(NFS_GUEST_FAST OR NFS_GUEST_NO_YIELD)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "NFS_GUEST_FAST / NFS_GUEST_NO_YIELD need arm64-v8a")
    endif()
endif()
if(NFS_GUEST_FAST)
    foreach(target nfs_guest nfsmw_android)
        target_compile_definitions(${target} PRIVATE NFS_GUEST_FAST NFS_GUEST_FIXED_BASE=0x100000000ull)
        target_compile_options(${target} PRIVATE -fomit-frame-pointer)
    endforeach()
endif()
if(NFS_GUEST_NO_YIELD)
    target_compile_definitions(nfs_guest PRIVATE NFS_GUEST_NO_YIELD)
endif()
# Instrumented profile-guided optimisation (as in the NFSU2 port, +32% fps
# there). -PnfsPgo=generate builds an instrumented APK that writes
# <external files>/pgo/default_<hash>.profraw when the file
# <external files>/pgo/dump appears; llvm-profdata merge -output=nfsmw.profdata
# *.profraw, then -PnfsPgo=use -PnfsPgoProfile=<path>. generate and use must
# agree on every other flag (NFS_GUEST_CPU/FAST/NO_YIELD/OPT, lift output): the
# profile is keyed by function name and control-flow hash and silently drops
# mismatches.
set(NFS_PGO "" CACHE STRING "Profile-guided optimisation: empty, generate or use")
set(NFS_PGO_PROFILE "" CACHE FILEPATH "Merged .profdata for NFS_PGO=use")
if(NFS_PGO STREQUAL "generate")
    foreach(target nfs_guest nfsmw_android)
        target_compile_options(${target} PRIVATE -fprofile-generate -fprofile-update=atomic)
    endforeach()
    target_compile_definitions(nfsmw_android PRIVATE NFS_PGO_GENERATE)
    target_link_options(nfsmw_android PRIVATE -fprofile-generate)
elseif(NFS_PGO STREQUAL "use")
    if(NOT EXISTS "${NFS_PGO_PROFILE}")
        message(FATAL_ERROR "NFS_PGO=use needs NFS_PGO_PROFILE=<merged .profdata>")
    endif()
    foreach(target nfs_guest nfsmw_android)
        target_compile_options(${target} PRIVATE "-fprofile-use=${NFS_PGO_PROFILE}"
            -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-profile-instr-missing -Wno-backend-plugin)
    endforeach()
    target_link_options(nfsmw_android PRIVATE "-fprofile-use=${NFS_PGO_PROFILE}")
elseif(NOT NFS_PGO STREQUAL "")
    message(FATAL_ERROR "NFS_PGO must be empty, generate or use")
endif()
target_link_libraries(nfsmw_android PRIVATE native_app_glue android log vulkan dxvk_android aaudio dl)
if(NFS_GUEST_LTO)
    target_link_options(nfsmw_android PRIVATE -flto=thin "-Wl,--thinlto-jobs=4"
        "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/thinlto-cache")
endif()
# linkernsbypass has data symbols named like libdl's functions; exporting them breaks
# the system loader when it opens the vendor namespace (MW port note).
# Only adrenotools' archives: excluding ALL would also hide native_app_glue's
# ANativeActivity_onCreate, which NativeActivity looks up by name.
foreach(target nfsmw_android nfsmw_diagnostics)
    target_link_options(${target} PRIVATE "-Wl,--exclude-libs,libadrenotools.a:liblinkernsbypass.a" "-Wl,-z,max-page-size=16384")
endforeach()
if(NFS_CUSTOM_DRIVERS)
    foreach(target nfsmw_android nfsmw_diagnostics)
        target_link_libraries(${target} PRIVATE adrenotools)
        target_compile_definitions(${target} PRIVATE NFS_CUSTOM_DRIVERS)
    endforeach()
    target_link_options(hook_impl PRIVATE "-Wl,--exclude-libs,ALL")
    foreach(target adrenotools main_hook file_redirect_hook gsl_alloc_hook hook_impl)
        target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
    endforeach()
    # adrenotools opens these from nativeLibraryDir at run time.
    add_dependencies(nfsmw_android main_hook file_redirect_hook gsl_alloc_hook hook_impl)
endif()
