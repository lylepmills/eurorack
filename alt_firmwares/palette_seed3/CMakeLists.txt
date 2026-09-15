cmake_minimum_required(VERSION 3.22)
include(libDaisy/cmake/toolchains/autodetect.cmake)
project(palette_seed3 C CXX ASM)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
# Keep per-object sections visible to the placement scripts (no LTO).
add_compile_options(-O3)
add_subdirectory(libDaisy)
target_compile_definitions(daisy PUBLIC BOOT_APP)
get_filename_component(PALETTE_REPO ../.. ABSOLUTE)
option(PALETTE_EXTENDED_TZFM "Enable higher-compute Palette TZFM engines" ON)
option(PALETTE_TZFM_BENCH "Exercise signed FM during the CPU sweep" OFF)
option(PALETTE_SKIP_BENCH "Play immediately after boot" OFF)
set(PALETTE_PLAY_ENGINE_ID virtual-analog CACHE STRING "Engine to play after bench")
function(palette_target name set layout)
  include(generated/${set}/sources.cmake)
  string(TOUPPER ${set} set_upper)
  add_executable(${name} src/main.cc src/user_data.cc ${PALETTE_${set_upper}_SOURCES})
  target_include_directories(${name} PRIVATE ${PALETTE_REPO} generated/${set})
  target_link_libraries(${name} PRIVATE daisy m)
  target_compile_definitions(${name} PRIVATE
    PLAITS_BUILD_LINEAR_TZFM=1
    PLAITS_BUILD_EXTENDED_TZFM=$<BOOL:${PALETTE_EXTENDED_TZFM}>
    PALETTE_TZFM_BENCH=$<BOOL:${PALETTE_TZFM_BENCH}>
    PALETTE_SKIP_BENCH=$<BOOL:${PALETTE_SKIP_BENCH}>
    PALETTE_PLAY_ENGINE_ID="${PALETTE_PLAY_ENGINE_ID}"
    PALETTE_VARIANT="${layout}")
  set(script ${CMAKE_CURRENT_SOURCE_DIR}/ld/palette_${layout}.lds)
  set_target_properties(${name} PROPERTIES SUFFIX .elf LINK_DEPENDS ${script})
  target_link_options(${name} PRIVATE -T${script} -Wl,--gc-sections
    -Wl,-Map=${name}.map -Wl,--print-memory-usage)
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${name}>
      ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin)
endfunction()
palette_target(va_seed3 va sram)
palette_target(palette96_qspi all qspi)
palette_target(palette96_qspi_notables all qspi_notables)
palette_target(palette24_sram stock24 sram)
