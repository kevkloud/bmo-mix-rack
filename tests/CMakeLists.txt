# DSP tests build everywhere, JUCE or not. Plugin tests need JUCE.

# Shared DSP in core/dsp, header-only, so it links nothing.
bmo_add_dsp_tool(switch_fade_tests dsp/SwitchFadeTests.cpp)
add_test(NAME switch_fade COMMAND switch_fade_tests)

if(BMO_BUILD_RACK)

bmo_add_dsp_tool(eq_dsp_tests dsp/EqDspTests.cpp)
target_link_libraries(eq_dsp_tests PRIVATE bmo_eq_dsp)
add_test(NAME eq_dsp COMMAND eq_dsp_tests)

bmo_add_dsp_tool(eq_switch_tests dsp/EqSwitchTests.cpp)
target_link_libraries(eq_switch_tests PRIVATE bmo_eq_dsp)
add_test(NAME eq_switch COMMAND eq_switch_tests)

bmo_add_dsp_tool(sat_dsp_tests dsp/SatDspTests.cpp)
target_link_libraries(sat_dsp_tests PRIVATE bmo_sat_dsp)
add_test(NAME sat_dsp COMMAND sat_dsp_tests)

bmo_add_dsp_tool(sat_switch_tests dsp/SatSwitchTests.cpp)
target_link_libraries(sat_switch_tests PRIVATE bmo_sat_dsp)
add_test(NAME sat_switch COMMAND sat_switch_tests)

bmo_add_dsp_tool(util_dsp_tests dsp/UtilDspTests.cpp)
target_link_libraries(util_dsp_tests PRIVATE bmo_util_dsp)
add_test(NAME util_dsp COMMAND util_dsp_tests)

bmo_add_dsp_tool(opto_dsp_tests dsp/OptoDspTests.cpp)
target_link_libraries(opto_dsp_tests PRIVATE bmo_opto_dsp)
add_test(NAME opto_dsp COMMAND opto_dsp_tests)

bmo_add_dsp_tool(opto_switch_tests dsp/OptoSwitchTests.cpp)
target_link_libraries(opto_switch_tests PRIVATE bmo_opto_dsp)
add_test(NAME opto_switch COMMAND opto_switch_tests)

bmo_add_dsp_tool(dim_dsp_tests dsp/DimDspTests.cpp)
target_link_libraries(dim_dsp_tests PRIVATE bmo_dim_dsp)
add_test(NAME dim_dsp COMMAND dim_dsp_tests)

bmo_add_dsp_tool(deq_dsp_tests dsp/DeqDspTests.cpp)
target_link_libraries(deq_dsp_tests PRIVATE bmo_deq_dsp)
add_test(NAME deq_dsp COMMAND deq_dsp_tests)

bmo_add_dsp_tool(vcomp_dsp_tests dsp/VcompDspTests.cpp)
target_link_libraries(vcomp_dsp_tests PRIVATE bmo_vcomp_dsp)
add_test(NAME vcomp_dsp COMMAND vcomp_dsp_tests)

bmo_add_dsp_tool(deesser_dsp_tests dsp/DeesserDspTests.cpp)
target_link_libraries(deesser_dsp_tests PRIVATE bmo_deesser_dsp)
add_test(NAME deesser_dsp COMMAND deesser_dsp_tests)
bmo_add_dsp_tool(fetcomp_dsp_tests dsp/FetcompDspTests.cpp)
target_link_libraries(fetcomp_dsp_tests PRIVATE bmo_fetcomp_dsp)
add_test(NAME fetcomp_dsp COMMAND fetcomp_dsp_tests)
bmo_add_dsp_tool(dwell_dsp_tests dsp/DwellDspTests.cpp)
target_link_libraries(dwell_dsp_tests PRIVATE bmo_dwell_dsp)
add_test(NAME dwell_dsp COMMAND dwell_dsp_tests)

bmo_add_dsp_tool(reverb_dsp_tests dsp/ReverbDspTests.cpp)
target_link_libraries(reverb_dsp_tests PRIVATE bmo_reverb_dsp)
add_test(NAME reverb_dsp COMMAND reverb_dsp_tests)

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

bmo_add_tool(deesser_tests plugin/DeesserTests.cpp)
target_link_libraries(deesser_tests PRIVATE bmo_deesser)
add_test(NAME deesser COMMAND deesser_tests)

bmo_add_tool(fetcomp_tests plugin/FetcompTests.cpp)
target_link_libraries(fetcomp_tests PRIVATE bmo_fetcomp)
add_test(NAME fetcomp COMMAND fetcomp_tests)

bmo_add_tool(dwell_tests plugin/DwellTests.cpp)
target_link_libraries(dwell_tests PRIVATE bmo_dwell)
add_test(NAME dwell COMMAND dwell_tests)

bmo_add_tool(reverb_tests plugin/ReverbTests.cpp)
target_link_libraries(reverb_tests PRIVATE bmo_reverb)
# The one plugin test that restores from a thread other than the message
# thread, and it has to deliver what that restore queued before it can assert
# anything -- which is MessageManager::runDispatchLoopUntil, and JUCE only
# compiles that in with modal loops permitted. Every JUCE source is compiled
# into the executable itself, so the define is consistent across it.
target_compile_definitions(reverb_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
add_test(NAME reverb COMMAND reverb_tests)

bmo_add_tool(rack_tests plugin/RackTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(rack_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
# As reverb_tests: the off-thread restore needs a message loop to grant its
# MessageManagerLock and to deliver what it queued.
target_compile_definitions(rack_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
add_test(NAME rack COMMAND rack_tests)

# Bus layouts, across every product at once: the accepted-layout table, and the
# golden renders that prove mono-in/stereo-out cost the existing layouts
# nothing. Linked like rack_tests because it walks the registry.
bmo_add_tool(bus_tests plugin/BusTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(bus_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
add_test(NAME bus COMMAND bus_tests)

# Tail-length reporting, across every product at once: the exact zero every
# shipped module is required to report, BMO Linger's own stated figures, and
# the rack's sum over its occupied slots. Linked like bus_tests because it
# walks the registry.
bmo_add_tool(tail_tests plugin/TailTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(tail_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
add_test(NAME tail COMMAND tail_tests)

# The host's tempo, handed to every module once per block: the order and the
# three fallbacks through both processors, a probe module in two rack slots,
# and every registered module byte-identical with and without a playhead.
# Linked like tail_tests because it walks the registry.
bmo_add_tool(tempo_tests plugin/TempoTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(tempo_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
# BMO Tune RT runs on the same SingleModuleProcessor but is not in the rack's
# registry, so it is linked in by hand -- and only when the build has it, so
# the rack-alone configuration (BMO_BUILD_TUNE=OFF) still builds this suite.
if(BMO_BUILD_TUNE)
    target_link_libraries(tempo_tests PRIVATE bmo_tune)
    target_compile_definitions(tempo_tests PRIVATE BMO_TEMPO_TESTS_TUNE=1)
endif()
add_test(NAME tempo COMMAND tempo_tests)

# One NaN or infinity into every registered module, through the processor a
# host drives, and into a rack at its input and mid-chain: nothing non-finite
# leaves, and the module recovers by itself. Linked like tempo_tests, Tune
# included, because it walks the registry and the guard is ModuleEngine's.
bmo_add_tool(finite_tests plugin/FiniteTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(finite_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
if(BMO_BUILD_TUNE)
    target_link_libraries(finite_tests PRIVATE bmo_tune)
    target_compile_definitions(finite_tests PRIVATE BMO_FINITE_TESTS_TUNE=1)
endif()
add_test(NAME finite COMMAND finite_tests)

# reset(), latency and tail before any prepare, and release twice, for every
# module, in the standalone processor and in a rack slot. A call that does not
# return fails the suite within seconds rather than stalling ctest. Linked like
# finite_tests, Tune included.
bmo_add_tool(lifecycle_tests plugin/LifecycleTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(lifecycle_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
if(BMO_BUILD_TUNE)
    target_link_libraries(lifecycle_tests PRIVATE bmo_tune)
    target_compile_definitions(lifecycle_tests PRIVATE BMO_LIFECYCLE_TESTS_TUNE=1)
endif()
add_test(NAME lifecycle COMMAND lifecycle_tests)

# How long each module takes to come back from one very loud sample under the
# guard's ceiling: recorded figures, not targets (see the file). Up to 100 s
# of audio per case, so it is the slowest suite here. Linked like finite_tests,
# Tune included.
bmo_add_tool(recovery_tests plugin/RecoveryTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(recovery_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
if(BMO_BUILD_TUNE)
    target_link_libraries(recovery_tests PRIVATE bmo_tune)
    target_compile_definitions(recovery_tests PRIVATE BMO_RECOVERY_TESTS_TUNE=1)
endif()
add_test(NAME recovery COMMAND recovery_tests)

# Layout assertions on the real panels. JUCE, not bmo_add_dsp_tool: no
# rendering happens, but Tokens.h includes juce_gui_basics, so a UI assertion
# cannot run in the DSP-only job whatever docs/ui-workflow-brief.md §4 says.
bmo_add_tool(ui_layout_tests ui/LayoutTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(ui_layout_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
add_test(NAME ui_layout COMMAND ui_layout_tests)

# The shared controls in core/ui, asserted on their own rather than through a
# panel that happens to use one. Links `bmo_core` and no module: the claims are
# the control's, and `ui::Fader` has no call site yet -- BMO Linger's panel is
# the pass after the one that added it.
bmo_add_tool(ui_control_tests ui/ControlTests.cpp)
target_link_libraries(ui_control_tests PRIVATE bmo_core)
add_test(NAME ui_control COMMAND ui_control_tests)

# The render loop's own plumbing. Links no module and instantiates no panel: it
# asserts that writing a PNG over an existing one replaces it, after a period
# where `snapshot` appended instead and every render reviewed was the previous
# one. Cheap enough to always run -- it writes three small images to temp.
bmo_add_tool(snapshot_io_tests tools/SnapshotIoTests.cpp)
# Links no module, so unlike its neighbours it inherits no include path from
# one: it needs the root spelled out to reach tools/snapshot/PngOut.h.
target_include_directories(snapshot_io_tests PRIVATE ${BMO_ROOT})
add_test(NAME snapshot_io COMMAND snapshot_io_tests)

endif()

# No juce::Image with static storage in the UI code: a source scan, because the
# fault it holds (an image outliving every editor hangs the host at unload)
# needs a loaded plugin to show. JUCE-free, and outside the rack switch, since
# it reads products/ and every module's panel whatever this build compiles.
bmo_add_dsp_tool(ui_static_image_tests ui/StaticImageTests.cpp)
add_test(NAME ui_static_image COMMAND ui_static_image_tests ${BMO_ROOT})

# Tune's real panel, laid out with no display. Its layout is asserted here
# rather than in ui/LayoutTests.cpp, which walks the rack's registry.
if(BMO_BUILD_TUNE)
    bmo_add_tool(tune_panel_tests plugin/tune/PanelTests.cpp)
    target_include_directories(tune_panel_tests PRIVATE ${BMO_ROOT})
    target_link_libraries(tune_panel_tests PRIVATE bmo_tune)
    add_test(NAME tune_panel COMMAND tune_panel_tests)
endif()
