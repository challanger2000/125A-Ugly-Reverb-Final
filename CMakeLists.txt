cmake_minimum_required(VERSION 3.20)

project(UglyReverb VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  vst3sdk
  GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
  GIT_TAG 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
  GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(vst3sdk)

smtg_enable_vst3_sdk()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resource/125A_Logo_Master_FINAL.svg" UGLY_BRANDING_SVG)
string(REGEX MATCHALL "<path d=\\\"[^\\\"]+\\\" fill=\\\"#[0-9A-Fa-f]+\\\"/>" UGLY_BRANDING_PATH_TAGS "${UGLY_BRANDING_SVG}")
list(LENGTH UGLY_BRANDING_PATH_TAGS UGLY_BRANDING_PATH_COUNT)
if(NOT UGLY_BRANDING_PATH_COUNT EQUAL 30)
  message(FATAL_ERROR "125A branding master must contain exactly 30 vector paths; found ${UGLY_BRANDING_PATH_COUNT}.")
endif()
set(UGLY_BRANDING_PATHS "")
foreach(UGLY_BRANDING_TAG IN LISTS UGLY_BRANDING_PATH_TAGS)
  if(NOT UGLY_BRANDING_TAG MATCHES "^<path d=\\\"([^\\\"]+)\\\" fill=\\\"([^\\\"]+)\\\"/>$")
    message(FATAL_ERROR "Could not parse 125A branding path tag.")
  endif()
  set(UGLY_BRANDING_D "${CMAKE_MATCH_1}")
  set(UGLY_BRANDING_FILL "${CMAKE_MATCH_2}")
  if(UGLY_BRANDING_FILL STREQUAL "#D71920")
    set(UGLY_BRANDING_RED "true")
  elseif(UGLY_BRANDING_FILL STREQUAL "#D9D9D9")
    set(UGLY_BRANDING_RED "false")
  else()
    message(FATAL_ERROR "Unexpected 125A branding fill: ${UGLY_BRANDING_FILL}")
  endif()
  string(APPEND UGLY_BRANDING_PATHS "    {\"${UGLY_BRANDING_D}\", ${UGLY_BRANDING_RED}},\n")
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/source/branding_master.h.in" "${CMAKE_BINARY_DIR}/branding_master.h" @ONLY)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(UGLY_GENERATED_RESOURCE_DIR "${CMAKE_BINARY_DIR}/generated_resources")
file(MAKE_DIRECTORY "${UGLY_GENERATED_RESOURCE_DIR}")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_gui_overlays.py" "${UGLY_GENERATED_RESOURCE_DIR}"
  RESULT_VARIABLE UGLY_OVERLAY_RESULT
  OUTPUT_VARIABLE UGLY_OVERLAY_OUTPUT
  ERROR_VARIABLE UGLY_OVERLAY_ERROR
)
if(NOT UGLY_OVERLAY_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate GUI overlays: ${UGLY_OVERLAY_ERROR}")
endif()
message(STATUS "${UGLY_OVERLAY_OUTPUT}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_knob_strips.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/resource/fender_amp1.png"
          "${UGLY_GENERATED_RESOURCE_DIR}"
  RESULT_VARIABLE UGLY_KNOB_RESULT
  OUTPUT_VARIABLE UGLY_KNOB_OUTPUT
  ERROR_VARIABLE UGLY_KNOB_ERROR
)
if(NOT UGLY_KNOB_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate knob strips: ${UGLY_KNOB_ERROR}")
endif()
message(STATUS "${UGLY_KNOB_OUTPUT}")


smtg_add_vst3plugin(UglyReverb
  source/version.h
  source/ids.h
  source/parameters.h
  source/state_format.h
  source/processor.h
  source/processor.cpp
  source/controller.h
  source/controller.cpp
  source/UglyControls.h
  source/UglyControls.cpp
  source/entry.cpp
  resource/ugly_reverb.uidesc
)

target_link_libraries(UglyReverb PRIVATE sdk vstgui_support)
target_include_directories(UglyReverb PRIVATE "${CMAKE_BINARY_DIR}")

smtg_target_add_plugin_resources(UglyReverb
  RESOURCES
    resource/ugly_reverb.uidesc
    resource/125A_Logo_Master_FINAL.svg
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_wear_overlay.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_wear_overlay#2.0x.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_glass_overlay.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_glass_overlay#2.0x.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_knob_62.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_knob_62#2.0x.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_knob_64.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_knob_64#2.0x.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_knob_66.png"
    "${UGLY_GENERATED_RESOURCE_DIR}/ugly_knob_66#2.0x.png"
)

smtg_target_configure_version_file(UglyReverb)

add_executable(UglyReverbMeasurements
  tests/dsp_measurements.cpp
  source/processor.cpp
  ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
  ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
)

if(WIN32)
  add_executable(UglyReverbReloadProbe
    tests/reload_lifecycle.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
  )
  target_link_libraries(UglyReverbReloadProbe PRIVATE sdk_hosting sdk_common base pluginterfaces)
  target_compile_definitions(UglyReverbReloadProbe PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
target_compile_definitions(UglyReverbMeasurements PRIVATE UGLY_REVERB_TESTING=1)
target_include_directories(UglyReverbMeasurements PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(UglyReverbMeasurements PRIVATE sdk vstgui_support)

enable_testing()
add_test(NAME UglyReverbMeasurements COMMAND UglyReverbMeasurements)

if(SMTG_WIN AND MSVC)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT UglyReverb)
endif()
