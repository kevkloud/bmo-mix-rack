# DSP tests build everywhere, JUCE or not. Plugin tests need JUCE.

if(BMO_BUILD_RACK)

bmo_add_dsp_tool(eq_dsp_tests dsp/EqDspTests.cpp)
target_link_libraries(eq_dsp_tests PRIVATE bmo_eq_dsp)
add_test(NAME eq_dsp COMMAND eq_dsp_tests)

bmo_add_dsp_tool(sat_dsp_tests dsp/SatDspTests.cpp)
target_link_libraries(sat_dsp_tests PRIVATE bmo_sat_dsp)
add_test(NAME sat_dsp COMMAND sat_dsp_tests)

bmo_add_dsp_tool(util_dsp_tests dsp/UtilDspTests.cpp)
target_link_libraries(util_dsp_tests PRIVATE bmo_util_dsp)
add_test(NAME util_dsp COMMAND util_dsp_tests)

bmo_add_dsp_tool(opto_dsp_tests dsp/OptoDspTests.cpp)
target_link_libraries(opto_dsp_tests PRIVATE bmo_opto_dsp)
add_test(NAME opto_dsp COMMAND opto_dsp_tests)

bmo_add_dsp_tool(dim_dsp_tests dsp/DimDspTests.cpp)
target_link_libraries(dim_dsp_tests PRIVATE bmo_dim_dsp)
add_test(NAME dim_dsp COMMAND dim_dsp_tests)

bmo_add_dsp_tool(deq_dsp_tests dsp/DeqDspTests.cpp)
target_link_libraries(deq_dsp_tests PRIVATE bmo_deq_dsp)
add_test(NAME deq_dsp COMMAND deq_dsp_tests)

bmo_add_dsp_tool(vcomp_dsp_tests dsp/VcompDspTests.cpp)
target_link_libraries(vcomp_dsp_tests PRIVATE bmo_vcomp_dsp)
add_test(NAME vcomp_dsp COMMAND vcomp_dsp_tests)

endif()

# BMO Tune RT's suites. Their sources keep the names they were written under,
# in a directory of their own: CoreTests and SchemaTests say nothing about
# whose they are, and a second module writing a file by either name would
# collide. Every ctest name is prefixed tune_ for the same reason.
if(BMO_BUILD_TUNE)

function(bmo_tune_test name source)
    bmo_add_dsp_tool(tune_${name}_tests ${CMAKE_CURRENT_SOURCE_DIR}/dsp/tune/${source})
    target_link_libraries(tune_${name}_tests PRIVATE bmo_tune_dsp)
    add_test(NAME tune_${name} COMMAND tune_${name}_tests)
endfunction()

bmo_tune_test(kernel       KernelTests.cpp)
bmo_tune_test(detector     DetectorTests.cpp)
bmo_tune_test(interpolator InterpolatorTests.cpp)
bmo_tune_test(correction   CorrectionTests.cpp)
bmo_tune_test(core         CoreTests.cpp)
bmo_tune_test(schema       SchemaTests.cpp)
bmo_tune_test(hardtune     HardTuneTests.cpp)
bmo_tune_test(voice        VoiceTests.cpp)

# The hard-tune target: BMO as close as Antares on a moving voice. It fails
# today -- that is the point of it (the house rule: fix nothing a test does not
# first fail on) -- so it is registered but DISABLED, which ctest lists as
# "Not Run (Disabled)" at the end of every run rather than hiding it. Enable
# it in the change that makes it pass. Run it by hand:
#   build/tests/Release/tune_hardtune_tests --target
add_test(NAME tune_hardtune_target COMMAND tune_hardtune_tests --target)
set_tests_properties(tune_hardtune_target PROPERTIES DISABLED TRUE LABELS open)

endif()

if(BMO_DSP_ONLY)
    return()
endif()

if(BMO_BUILD_RACK)

bmo_add_tool(eq_tests plugin/EqTests.cpp)
target_link_libraries(eq_tests PRIVATE bmo_eq)
add_test(NAME eq COMMAND eq_tests)

bmo_add_tool(sat_tests plugin/SatTests.cpp)
target_link_libraries(sat_tests PRIVATE bmo_sat)
add_test(NAME sat COMMAND sat_tests)

bmo_add_tool(util_tests plugin/UtilTests.cpp)
target_link_libraries(util_tests PRIVATE bmo_util)
add_test(NAME util COMMAND util_tests)

bmo_add_tool(opto_tests plugin/OptoTests.cpp)
target_link_libraries(opto_tests PRIVATE bmo_opto)
add_test(NAME opto COMMAND opto_tests)

bmo_add_tool(dim_tests plugin/DimTests.cpp)
target_link_libraries(dim_tests PRIVATE bmo_dim)
add_test(NAME dim COMMAND dim_tests)

bmo_add_tool(deq_tests plugin/DeqTests.cpp)
target_link_libraries(deq_tests PRIVATE bmo_deq)
add_test(NAME deq COMMAND deq_tests)

bmo_add_tool(vcomp_tests plugin/VcompTests.cpp)
target_link_libraries(vcomp_tests PRIVATE bmo_vcomp)
add_test(NAME vcomp COMMAND vcomp_tests)

bmo_add_tool(rack_tests plugin/RackTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(rack_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp)
add_test(NAME rack COMMAND rack_tests)

# Layout assertions on the real panels. JUCE, not bmo_add_dsp_tool: no
# rendering happens, but Tokens.h includes juce_gui_basics, so a UI assertion
# cannot run in the DSP-only job whatever docs/ui-workflow-brief.md §4 says.
bmo_add_tool(ui_layout_tests ui/LayoutTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(ui_layout_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp)
add_test(NAME ui_layout COMMAND ui_layout_tests)

endif()

# Tune's real panel, laid out with no display. Its layout is asserted here
# rather than in ui/LayoutTests.cpp, which walks the rack's registry.
if(BMO_BUILD_TUNE)
    bmo_add_tool(tune_panel_tests plugin/tune/PanelTests.cpp)
    target_include_directories(tune_panel_tests PRIVATE ${BMO_ROOT})
    target_link_libraries(tune_panel_tests PRIVATE bmo_tune)
    add_test(NAME tune_panel COMMAND tune_panel_tests)
endif()
