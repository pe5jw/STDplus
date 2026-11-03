# STDplus — addon build on top of STD (upstream as submodule at std/)
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)

project(STDplus
  VERSION 0.1.0
  DESCRIPTION "STD with dialect detection, CW modem, N1MM CAT bridge, and voice keyer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(STD_DIR "${CMAKE_SOURCE_DIR}/std")

if(WIN32 AND EXISTS "C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6/Qt6Config.cmake")
  list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network WebSockets GuiPrivate)
find_package(Qt6 OPTIONAL_COMPONENTS Vulkan)
find_package(Vulkan QUIET)
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# STD sources (from submodule, unmodified) — std/src/main.cpp is replaced
# by our own src/app/main.cpp below.
# ---------------------------------------------------------------------------
set(STD_SOURCES
  ${STD_DIR}/src/MainWindow.cpp
  ${STD_DIR}/src/MainWindowJs8.cpp
  ${STD_DIR}/src/MainWindowWefax.cpp
  ${STD_DIR}/src/MainWindowQso.cpp
  ${STD_DIR}/src/MaEngine.cpp
  ${STD_DIR}/src/WaterfallWidget.cpp
  ${STD_DIR}/src/WaterfallGpu.cpp
  ${STD_DIR}/src/ModeProfile.cpp
  ${STD_DIR}/src/Ft8Decode.cpp
  ${STD_DIR}/src/StatusStrip.cpp
  ${STD_DIR}/src/TciClient.cpp
  ${STD_DIR}/src/TxWave.cpp
  ${STD_DIR}/src/QsoLog.cpp
  ${STD_DIR}/src/StationFiles.cpp
  ${STD_DIR}/src/MacroFile.cpp
  ${STD_DIR}/src/UpdateChecker.cpp
  ${STD_DIR}/src/hell/HellEngine.cpp
  ${STD_DIR}/src/hell/HellRaster.cpp
  ${STD_DIR}/src/hell/fldigi/filters.cxx
  ${STD_DIR}/src/hell/fldigi/fftfilt.cxx
  ${STD_DIR}/src/rtty/RttyModem.cpp
  ${STD_DIR}/src/psk/PskModem.cpp
  ${STD_DIR}/src/wefax/WefaxModem.cpp
  ${STD_DIR}/src/olivia/OliviaModem.cpp
  ${STD_DIR}/src/mfsk/MfskModem.cpp
  ${STD_DIR}/src/dominoex/DominoModem.cpp
  ${STD_DIR}/src/thor/ThorModem.cpp
  ${STD_DIR}/src/mt63/Mt63Modem.cpp
  ${STD_DIR}/src/mt63/fldigi/dsp.cxx
  ${STD_DIR}/src/mt63/fldigi/mt63base.cxx
  ${STD_DIR}/src/js8/Js8Modem.cpp
  ${STD_DIR}/src/wspr/WsprModem.cpp
  ${STD_DIR}/src/spot/PskReporter.cpp
  ${STD_DIR}/src/spot/WsprNet.cpp
  ${STD_DIR}/resources/resources.qrc
  ${STD_DIR}/src/std.rc
)

# ---------------------------------------------------------------------------
# STDplus feature sources (our own code + replacement main)
# ---------------------------------------------------------------------------
set(STDPLUS_SOURCES
  src/app/main.cpp
  src/app/MainWindowPlus.cpp
  src/app/MainWindowPlus.h
  src/tci/TciDialect.cpp
  src/tci/TciDialect.h
  src/tci/TciClientPlus.cpp
  src/tci/TciClientPlus.h
  src/cw/CwModem.cpp
  src/cw/CwModem.h
  src/n1mm/N1mmCat.cpp
  src/n1mm/N1mmCat.h
  src/voicekey/VoiceKeyer.cpp
  src/voicekey/VoiceKeyer.h
)

qt_add_executable(stdplus
  WIN32
  ${STD_SOURCES}
  ${STDPLUS_SOURCES}
)

target_compile_definitions(stdplus PRIVATE
  STD_VERSION="${PROJECT_VERSION}"
  STD_REPO_OWNER="pe5jw"
  STD_REPO_NAME="STDplus"
  STDPLUS_VERSION="${PROJECT_VERSION}")

target_include_directories(stdplus PRIVATE
  "${STD_DIR}/src"
  "${STD_DIR}/src/hell/fldigi"
  "${STD_DIR}/src/mt63/fldigi"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/app"
  "${CMAKE_SOURCE_DIR}/src/tci"
  "${CMAKE_SOURCE_DIR}/src/cw"
  "${CMAKE_SOURCE_DIR}/src/n1mm"
  "${CMAKE_SOURCE_DIR}/src/voicekey")

# Vulkan headers (needed for WaterfallGpu.cpp)
if(Vulkan_FOUND)
  target_include_directories(stdplus PRIVATE ${Vulkan_INCLUDE_DIRS})
  message(STATUS "Vulkan SDK headers: ${Vulkan_INCLUDE_DIRS}")
elseif(TARGET Qt6::Vulkan)
  get_target_property(_vk_inc Qt6::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
  if(_vk_inc)
    target_include_directories(stdplus PRIVATE ${_vk_inc})
    message(STATUS "Vulkan headers via Qt6::Vulkan: ${_vk_inc}")
  endif()
else()
  message(STATUS "Vulkan SDK not found — WaterfallGpu may fail to compile")
endif()

target_link_libraries(stdplus PRIVATE
  Qt6::Widgets Qt6::GuiPrivate Qt6::Network Qt6::WebSockets
  $<$<TARGET_EXISTS:Qt6::Vulkan>:Qt6::Vulkan>
  $<$<TARGET_EXISTS:Vulkan::Vulkan>:Vulkan::Vulkan>)

if(MSVC)
  target_compile_options(stdplus PRIVATE /W4)
  set_source_files_properties(
    ${STD_DIR}/src/hell/fldigi/filters.cxx
    ${STD_DIR}/src/hell/fldigi/fftfilt.cxx
    ${STD_DIR}/src/mt63/fldigi/dsp.cxx
    ${STD_DIR}/src/mt63/fldigi/mt63base.cxx
    PROPERTIES COMPILE_OPTIONS "/W1")
else()
  target_compile_options(stdplus PRIVATE -Wall -Wextra -Wpedantic)
endif()

# FT8 decoder DLL
if(WIN32)
  set(MSHV_FT8_SCRIPT "${STD_DIR}/src/ft8/build_ft8dec.cmd")
  if(EXISTS "${MSHV_FT8_SCRIPT}")
    set(FT8_DLL "${CMAKE_BINARY_DIR}/ft8dec.dll")
    add_custom_command(
      OUTPUT "${FT8_DLL}"
      COMMAND "${CMAKE_COMMAND}"
        -DSCRIPT=${STD_DIR}/src/ft8/build_ft8dec.cmd
        -DOUT=${CMAKE_BINARY_DIR}
        -P ${STD_DIR}/src/ft8/run_ft8dec.cmake
      DEPENDS
        "${STD_DIR}/src/ft8/build_ft8dec.cmd"
        "${STD_DIR}/src/ft8/run_ft8dec.cmake"
        "${STD_DIR}/src/ft8/invoke_ft8dec.ps1"
        "${STD_DIR}/src/ft8/ft8_bridge.cpp"
      WORKING_DIRECTORY "${STD_DIR}"
      COMMENT "Building FT8 decoder DLL from MSHV"
      VERBATIM)
    add_custom_target(ft8dec DEPENDS "${FT8_DLL}")
    add_dependencies(stdplus ft8dec)
    message(STATUS "FT8 decoder DLL: enabled (MSHV found)")
  else()
    message(STATUS "FT8 decoder DLL: skipped (MSHV sources not found)")
  endif()

  get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
  find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)
  if(WINDEPLOYQT_EXE)
    add_custom_command(TARGET stdplus POST_BUILD
      COMMAND "${WINDEPLOYQT_EXE}" --no-translations "$<TARGET_FILE:stdplus>"
      COMMENT "windeployqt"
      VERBATIM)
  endif()
endif()

install(TARGETS stdplus RUNTIME DESTINATION bin)
