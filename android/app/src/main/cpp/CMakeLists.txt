cmake_minimum_required(VERSION 3.22.1)
project(sheepraider LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# cpp -> main -> src -> app -> android -> repo root
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
get_filename_component(REPO_ROOT "${REPO_ROOT}" ABSOLUTE)

# --- native_app_glue ---
add_library(native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue INTERFACE
    "${ANDROID_NDK}/sources/android/native_app_glue")
target_compile_options(native_app_glue PRIVATE -w)
target_compile_definitions(native_app_glue PRIVATE NDEBUG)
target_link_options(native_app_glue INTERFACE -u ANativeActivity_onCreate)

# --- PSXRecomp Runtime Core ---
set(PSX_RUNTIME_CORE
    "${REPO_ROOT}/psxrecomp/runtime/src/gte.cpp"
    "${REPO_ROOT}/psxrecomp/runtime/src/pgxp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/memory_android.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/psx_cycles.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/interrupts.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/traps.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/bios_hle.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/bios_hle_plan.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/psx_bios_backend.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/dirty_ram_interp.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/dma.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/dma_gpu_ll.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/gpu.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/gpu_render.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/gpu_vram_dirty.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/gpu_sw_renderer.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/cdrom.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/spu.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/spu_shadow.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/sio.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/timers.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/mdec.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/data_shards.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/host_time.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/psx_fiber_android.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/memcard.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/savestate.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/fntrace.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/boot_state.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/ws_ui_group.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/ws_aspect_cone_math.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/crc32.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/psx_sha256.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/psx_icache.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/load_accel.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/psx_iso_simple.cpp"
    "${REPO_ROOT}/psxrecomp/runtime/src/audio_shadow.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/card_read_summary.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/card_data_writes.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/kernel_patch_ranges.c"
    "${REPO_ROOT}/psxrecomp/runtime/src/color_lut.c"
)

# OpenBIOS
set(OPENBIOS_SOURCES
    "${REPO_ROOT}/generated/OpenBIOS_full.c"
    "${REPO_ROOT}/generated/OpenBIOS_dispatch.c"
)

# Game Shards
file(GLOB GAME_SHARDS "${REPO_ROOT}/generated/SLPM_870.56_full_*.c")

add_library(sheepraider SHARED
    AndroidShell.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/openbios_embedded.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/game_text_embedded.c"
    "${REPO_ROOT}/generated/SLPM_870.56_dispatch.c"
    ${GAME_SHARDS}
    ${PSX_RUNTIME_CORE}
    ${OPENBIOS_SOURCES}
)

target_include_directories(sheepraider PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPO_ROOT}/generated"
    "${REPO_ROOT}/psxrecomp/runtime/include"
    "${REPO_ROOT}/psxrecomp/recompiler/include"
)

target_compile_definitions(sheepraider PRIVATE
    ANDROID
    PSX_NATIVE_BUILD=1
    PSX_ENABLE_BLOCK_CYCLES=1
    PSX_HAS_GAME_DISPATCH=1
    PSX_NO_DEBUG_TOOLS=1
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(sheepraider PRIVATE
    native_app_glue
    android
    log
    EGL
    GLESv3
    m
    z
    aaudio
)

target_compile_options(sheepraider PRIVATE -O2 -w)
