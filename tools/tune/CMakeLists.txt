# BMO Tune RT's offline tools (spec T-1, T-3, T-5, T-11). All JUCE-free except
# the three at the bottom; all drive the real DSP library with no host
# involved. Named bmo-tune-* as the spec names them.

function(bmo_tune_tool name source)
    bmo_add_dsp_tool(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${source})
    target_link_libraries(${name} PRIVATE bmo_tune_dsp)
endfunction()

bmo_tune_tool(bmo-tune-cli cli/main.cpp)
bmo_tune_tool(bmo-tune-gen gen/main.cpp)
bmo_tune_tool(bmo-tune-score score/main.cpp)
bmo_tune_tool(bmo-tune-latency latency/main.cpp)
bmo_tune_tool(bmo-tune-bench bench/main.cpp)
bmo_tune_tool(bmo-tune-ref ref/main.cpp)
bmo_tune_tool(bmo-tune-blind blind/main.cpp)
bmo_tune_tool(bmo-tune-field field/main.cpp)

# The panel, rendered to a PNG with no display: needs JUCE and the product.
if(NOT BMO_DSP_ONLY)
    bmo_add_tool(bmo-tune-snapshot ${CMAKE_CURRENT_SOURCE_DIR}/snapshot/main.cpp)
    target_include_directories(bmo-tune-snapshot PRIVATE ${BMO_ROOT})
    target_link_libraries(bmo-tune-snapshot PRIVATE bmo_tune)
endif()

# Loads the built VST3 as a host would. Hosts VST3, so it needs JUCE's hosting
# switch, which no other target here wants.
if(NOT BMO_DSP_ONLY)
    bmo_add_tool(bmo-tune-hostcheck ${CMAKE_CURRENT_SOURCE_DIR}/hostcheck/main.cpp)
    target_include_directories(bmo-tune-hostcheck PRIVATE ${BMO_ROOT})
    target_compile_definitions(bmo-tune-hostcheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(bmo-tune-hostcheck PRIVATE bmo_tune_dsp)
    add_dependencies(bmo-tune-hostcheck BmoTuneRT_VST3)
    add_test(NAME tune_hostcheck COMMAND bmo-tune-hostcheck
             "$<TARGET_FILE_DIR:BmoTuneRT>/VST3/BMO Tune RT.vst3")
endif()

# Renders a WAV through any VST3, uncompensated -- how another tuner's true
# latency and correction lag are measured against ours (bmo-tune-ref score).
if(NOT BMO_DSP_ONLY)
    bmo_add_tool(bmo-tune-hostrender ${CMAKE_CURRENT_SOURCE_DIR}/hostrender/main.cpp)
    target_include_directories(bmo-tune-hostrender PRIVATE ${BMO_ROOT})
    target_compile_definitions(bmo-tune-hostrender PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(bmo-tune-hostrender PRIVATE bmo_tune_dsp)
endif()
