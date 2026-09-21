# The shared layer. An INTERFACE library with sources: every product compiles
# it into itself, because a static library would carry one product's
# JucePlugin_* definitions into the next.
# Shared DSP with a compiled part, and the only one so far: the matched-Z
# filter design, which BMO DEQ and BMO Defang both build their bands from.
#
# **A real static library rather than a source on bmo_core**, for two reasons
# that pull the same way. A module's DSP is a static library built from its own
# sources alone (`bmo_add_module`, modules/CMakeLists.txt) and does not link
# bmo_core, so a source listed there never reaches the DSP-only build at all --
# which is the configuration CI runs first. And bmo_core is an INTERFACE
# library whose sources are compiled into *every* product separately, so a
# product carrying two modules that both wanted this would compile it twice and
# fail to link on the duplicate. A static library is linked once either way.
#
# It must stay JUCE-free, like everything else under core/dsp.
add_library(bmo_dsp STATIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Design.cpp)
target_include_directories(bmo_dsp PUBLIC ${BMO_ROOT})
set_target_properties(bmo_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT BMO_DSP_ONLY)
    target_link_libraries(bmo_dsp PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

add_library(bmo_core INTERFACE)
target_include_directories(bmo_core INTERFACE ${BMO_ROOT})
target_link_libraries(bmo_core INTERFACE bmo_dsp)

if(NOT BMO_DSP_ONLY)
    target_sources(bmo_core INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/state/PresetManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Tokens.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Fonts.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/LookAndFeel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Controls.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/LevelBars.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Line.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ModulePanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/PresetBar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/product/SingleModuleProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/product/ProductEditor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rack/RackProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rack/RackEditor.cpp)

    target_link_libraries(bmo_core INTERFACE BmoAssets)
endif()
