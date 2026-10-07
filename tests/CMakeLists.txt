# DSP tests build everywhere, JUCE or not. Plugin tests need JUCE.

# The exhaustive run of a suite that keeps a subset by default: the same
# executable with `--long`, registered as `<name>_long` and labelled `long`,
# only when BMO_LONG_TESTS is ON (the option in the root CMakeLists.txt says
# why). The fence is one figure for all six, set by the slowest. Measured on
# the first CI run (ubuntu, four suites at a time, 2026-10-06): dwell_dsp
# 3661 s (its 360 rows are 26 minutes in Release on ICE QUEEN; the container
# is 2.3 times slower), reverb_dsp 870 s, vcomp_switch 301 s, eq_switch
# 191 s, sat_switch and deq_dsp 59 s each. 11000 s is three times the
# slowest honest run, as reverb_dsp's own 1350 s fence is three times its
# slowest Debug run. A run that reaches the fence still fails, as a timeout.
function(bmo_long_test name)
    if(BMO_LONG_TESTS)
        add_test(NAME ${name}_long COMMAND ${name}_tests --long)
        set_tests_properties(${name}_long PROPERTIES LABELS long TIMEOUT 11000)
    endif()
endfunction()

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
bmo_long_test(eq_switch)

bmo_add_dsp_tool(sat_dsp_tests dsp/SatDspTests.cpp)
target_link_libraries(sat_dsp_tests PRIVATE bmo_sat_dsp)
add_test(NAME sat_dsp COMMAND sat_dsp_tests)

bmo_add_dsp_tool(sat_switch_tests dsp/SatSwitchTests.cpp)
target_link_libraries(sat_switch_tests PRIVATE bmo_sat_dsp)
add_test(NAME sat_switch COMMAND sat_switch_tests)
bmo_long_test(sat_switch)

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
bmo_long_test(deq_dsp)

bmo_add_dsp_tool(vcomp_dsp_tests dsp/VcompDspTests.cpp)
target_link_libraries(vcomp_dsp_tests PRIVATE bmo_vcomp_dsp)
add_test(NAME vcomp_dsp COMMAND vcomp_dsp_tests)

bmo_add_dsp_tool(vcomp_switch_tests dsp/VcompSwitchTests.cpp)
target_link_libraries(vcomp_switch_tests PRIVATE bmo_vcomp_dsp)
add_test(NAME vcomp_switch COMMAND vcomp_switch_tests)
bmo_long_test(vcomp_switch)

bmo_add_dsp_tool(deesser_dsp_tests dsp/DeesserDspTests.cpp)
target_link_libraries(deesser_dsp_tests PRIVATE bmo_deesser_dsp)
add_test(NAME deesser_dsp COMMAND deesser_dsp_tests)
bmo_add_dsp_tool(fetcomp_dsp_tests dsp/FetcompDspTests.cpp)
target_link_libraries(fetcomp_dsp_tests PRIVATE bmo_fetcomp_dsp)
add_test(NAME fetcomp_dsp COMMAND fetcomp_dsp_tests)
bmo_add_dsp_tool(dwell_dsp_tests dsp/DwellDspTests.cpp)
target_link_libraries(dwell_dsp_tests PRIVATE bmo_dwell_dsp)
add_test(NAME dwell_dsp COMMAND dwell_dsp_tests)
bmo_long_test(dwell_dsp)

bmo_add_dsp_tool(reverb_dsp_tests dsp/ReverbDspTests.cpp)
target_link_libraries(reverb_dsp_tests PRIVATE bmo_reverb_dsp)
add_test(NAME reverb_dsp COMMAND reverb_dsp_tests)
bmo_long_test(reverb_dsp)
# A regression that hangs has to fail here, not stall the job until the
# runner gives up: reset() before prepare() spun forever on 6067809 and only
# a probe run by hand found it. (ctest's own default is 1500 s.)
#
# **The fence is set by the Debug build**, because scripts/build.sh tests
# that one. On ICE QUEEN (2026-10-03) the default run takes 40 to 67 s in
# Release and 271 to 447 s in Debug -- most of the spread is other builds
# sharing the machine -- so the fence is 1350 s, three times the slowest.
# Of a 387 s Debug run, 250 s is "the tail never grows, at the rates where
# it did" (96 and 192 kHz, 20 s each); the kept-moving rows are 21 s. It
# was 300 s while the kept-moving test ran all 43 rows by default, which
# took 374 to 404 s in Debug and timed the suite out; those rows are behind
# --long now, which ctest does not pass. A run that reaches the fence still
# fails, as a timeout.
set_tests_properties(reverb_dsp PROPERTIES TIMEOUT 1350)

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

# The hard-tune target: BMO as close as Tuner A on a moving voice. It fails
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
# The same fence as reverb_dsp, for the same reason; this one takes seconds.
set_tests_properties(reverb PROPERTIES TIMEOUT 300)

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
# The host-bypass checks change a latency and need the message loop to deliver
# it, as a host's would.
target_compile_definitions(bus_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
add_test(NAME bus COMMAND bus_tests)

# Sessions saved by a shipped release restore in this build: the blobs a release
# wrote, restored into every product it shipped. Linked like rack_tests because
# the rack restores through the registry, and with the tuner where it is built,
# since a release shipped that too. The off-thread pass needs a message loop,
# as rack_tests does. plugin/SESSION-COMPAT.md says how to add a release.
bmo_add_tool(session_compat_tests plugin/SessionCompatTests.cpp ${BMO_ROOT}/products/rack/Registry.cpp)
target_link_libraries(session_compat_tests PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp bmo_deesser bmo_fetcomp bmo_dwell bmo_reverb)
target_compile_definitions(session_compat_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
if(BMO_BUILD_TUNE)
    target_include_directories(session_compat_tests PRIVATE ${BMO_ROOT})
    target_link_libraries(session_compat_tests PRIVATE bmo_tune)
    target_compile_definitions(session_compat_tests PRIVATE BMO_SESSION_COMPAT_TUNE=1)
endif()
add_test(NAME session_compat COMMAND session_compat_tests)

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
