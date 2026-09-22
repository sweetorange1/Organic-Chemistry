cmake_minimum_required(VERSION 3.22)
project(OrganicChemistry VERSION 1.0.0 LANGUAGES C CXX)

# ===== macOS：双架构 Universal Binary（同时输出 Intel x86_64 与 Apple Silicon arm64）=====
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS target architectures (Intel + Apple Silicon)" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===== JUCE 8.0.12（与 Pupon / CRTLoss 等既有插件保持一致）=====
include(FetchContent)
FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
)
FetchContent_MakeAvailable(juce)

# ===== 合成器（Synth）=====
# 与效果器项目的差异：
#   - IS_SYNTH TRUE：定义为乐器（无输入总线）
#   - NEEDS_MIDI_INPUT TRUE：接收宿主 MIDI 输入
#   - AU_MAIN_TYPE kAudioUnitType_MusicDevice：AU 注册为乐器（而非效果器）
#   - FORMATS VST3 AU Standalone：AU 仅 macOS 构建，Windows 下被 JUCE 自动忽略
option(ORGANIC_COPY_PLUGIN_AFTER_BUILD "Install plugins after building" ON)
juce_add_plugin(
        OrganicChemistry
        PRODUCT_NAME "Organic Chemistry"
        VERSION 1.0.0
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.OrganicChemistry"
        PLUGIN_MANUFACTURER_CODE Isac
        PLUGIN_CODE OrCh
        FORMATS VST3 AU Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_AUTO_MANIFEST FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD ${ORGANIC_COPY_PLUGIN_AFTER_BUILD}
)

juce_generate_juce_header(OrganicChemistry)

target_compile_definitions(OrganicChemistry
  PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(OrganicChemistry PRIVATE
  PluginProcessor.cpp
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h
  MoleculeModel.cpp
  MoleculeModel.h
  MoleculeCanvas.cpp
  MoleculeCanvas.h
  ElementBar.cpp
  ElementBar.h
  MoleculeAudioMapper.cpp
  MoleculeAudioMapper.h
  BellEngine.cpp
  BellEngine.h
  TestPanel.cpp
  TestPanel.h
  PresetMenu.cpp
  PresetMenu.h
  network/Version.cpp
  network/Version.h
  network/UpdateChecker.cpp
  network/UpdateChecker.h
  ui/UpdateDialog.cpp
  ui/UpdateDialog.h
)

# Standalone 需要 juce_audio_utils（其传递依赖 juce_audio_devices，用于音频设备 IO）；
# juce_dsp 提供 StateVariableTPTFilter / Reverb / DelayLine 等效果器
target_link_libraries(OrganicChemistry PRIVATE juce::juce_audio_utils juce::juce_dsp)

# 源码含中文注释，MSVC 需按 UTF-8 解释，避免乱码 / 潜在编译问题
if(MSVC)
    target_compile_options(OrganicChemistry PRIVATE /utf-8)
endif()

option(ORGANIC_BUILD_AUDIO_TESTS "Build offline audio regression tests" OFF)
if(ORGANIC_BUILD_AUDIO_TESTS)
    enable_testing()
    juce_add_console_app(OrganicChemistryAudioTests PRODUCT_NAME "Organic Chemistry Audio Tests")
    target_sources(OrganicChemistryAudioTests PRIVATE AudioTests.cpp)
    target_include_directories(OrganicChemistryAudioTests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/OrganicChemistry_artefacts/JuceLibraryCode")
    target_link_libraries(OrganicChemistryAudioTests PRIVATE
        OrganicChemistry juce::juce_audio_utils juce::juce_dsp)
    if(MSVC)
        target_compile_options(OrganicChemistryAudioTests PRIVATE /utf-8)
    endif()
    add_test(NAME OrganicChemistryAudioTests COMMAND OrganicChemistryAudioTests)
    set_tests_properties(OrganicChemistryAudioTests PROPERTIES
        PASS_REGULAR_EXPRESSION "Audio regression tests passed")
endif()

option(ORGANIC_BUILD_BELL_TESTS "Build Bell tone offline verification" OFF)
if(ORGANIC_BUILD_BELL_TESTS)
    juce_add_console_app(OrganicChemistryBellTests PRODUCT_NAME "Organic Chemistry Bell Tests")
    target_sources(OrganicChemistryBellTests PRIVATE BellTests.cpp)
    target_include_directories(OrganicChemistryBellTests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/OrganicChemistry_artefacts/JuceLibraryCode")
    target_link_libraries(OrganicChemistryBellTests PRIVATE
        OrganicChemistry juce::juce_audio_utils juce::juce_dsp)
    if(MSVC)
        target_compile_options(OrganicChemistryBellTests PRIVATE /utf-8)
    endif()
endif()
