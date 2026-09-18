# Each module is two targets: its JUCE-free DSP as a real static library, used
# by the DSP tests and the measurement tools, and the whole module -- DSP,
# panel, presets, definition -- as an INTERFACE library the products compile in.
#
# Adding a module: add its two lines here and register it in products/rack.

function(bmo_add_module name)
    cmake_parse_arguments(ARG "" "" "DSP_SOURCES;UI_SOURCES" ${ARGN})

    add_library(bmo_${name}_dsp STATIC ${ARG_DSP_SOURCES})
    target_include_directories(bmo_${name}_dsp PUBLIC ${BMO_ROOT})
    set_target_properties(bmo_${name}_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT BMO_DSP_ONLY)
        target_link_libraries(bmo_${name}_dsp PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    endif()

    if(NOT BMO_DSP_ONLY)
        add_library(bmo_${name} INTERFACE)
        target_sources(bmo_${name} INTERFACE ${ARG_DSP_SOURCES} ${ARG_UI_SOURCES})
        target_link_libraries(bmo_${name} INTERFACE bmo_core)
    endif()
endfunction()

# The rack's modules. Guarded as one block rather than line by line: with
# BMO_BUILD_RACK off, none of these targets exists and only BMO Tune RT is
# configured. The bodies are left as they were so this reads as a diff of
# one line at each end.
if(BMO_BUILD_RACK)

bmo_add_module(eq
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/eq/dsp/DspCore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/eq/dsp/EqNetwork.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/eq/panel/EqPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/eq/Module.cpp)

bmo_add_module(sat
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/sat/dsp/DspCore.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/sat/panel/SatPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/sat/Module.cpp)

bmo_add_module(util
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/util/dsp/UtilDsp.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/util/panel/UtilPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/util/Module.cpp)

bmo_add_module(opto
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/opto/dsp/DspCore.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/opto/panel/OptoPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/opto/Module.cpp)

bmo_add_module(dim
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/dim/dsp/DspCore.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/dim/panel/DimPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dim/Module.cpp)

bmo_add_module(deq
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/deq/dsp/Design.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/deq/dsp/DspCore.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/deq/panel/DeqPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/deq/panel/ResponseView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/deq/Module.cpp)

bmo_add_module(vcomp
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/vcomp/dsp/DspCore.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/vcomp/panel/VcompPanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/vcomp/panel/LevelBars.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/vcomp/Module.cpp)

endif()

# BMO Tune RT. The same two targets as a rack module -- bmo_tune_dsp for the
# tests and the measurement tools, bmo_tune for the plugin -- because the shape
# is useful, not because it is in the rack. It is not registered anywhere, so
# adding it to a rack chain would not compile.
if(BMO_BUILD_TUNE)

bmo_add_module(tune
    DSP_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/tune/dsp/Detector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tune/dsp/CorrectionLaw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tune/dsp/ClassicEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tune/dsp/TuneCore.cpp
    UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/tune/panel/TunePanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tune/Module.cpp)

endif()
