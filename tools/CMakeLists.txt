if(BMO_BUILD_RACK)

# Offline measurement harnesses. Drive a module's DSP with no host involved.
bmo_add_dsp_tool(measure_eq  ${CMAKE_CURRENT_SOURCE_DIR}/measure/eq/main.cpp)
target_link_libraries(measure_eq PRIVATE bmo_eq_dsp)

bmo_add_dsp_tool(measure_sat ${CMAKE_CURRENT_SOURCE_DIR}/measure/sat/main.cpp)
target_link_libraries(measure_sat PRIVATE bmo_sat_dsp)

bmo_add_dsp_tool(measure_opto ${CMAKE_CURRENT_SOURCE_DIR}/measure/opto/main.cpp)
target_link_libraries(measure_opto PRIVATE bmo_opto_dsp)

bmo_add_dsp_tool(measure_dim  ${CMAKE_CURRENT_SOURCE_DIR}/measure/dim/main.cpp)
target_link_libraries(measure_dim PRIVATE bmo_dim_dsp)

bmo_add_dsp_tool(measure_deq  ${CMAKE_CURRENT_SOURCE_DIR}/measure/deq/main.cpp)
target_link_libraries(measure_deq PRIVATE bmo_deq_dsp)

bmo_add_dsp_tool(measure_vcomp ${CMAKE_CURRENT_SOURCE_DIR}/measure/vcomp/main.cpp)
target_link_libraries(measure_vcomp PRIVATE bmo_vcomp_dsp)

if(NOT BMO_DSP_ONLY)
    bmo_add_tool(snapshot
        ${CMAKE_CURRENT_SOURCE_DIR}/snapshot/main.cpp
        ${BMO_ROOT}/products/rack/Registry.cpp)
    target_link_libraries(snapshot PRIVATE bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp)
endif()

endif()

# BMO Tune RT's own harnesses, including its own snapshot: the rack's snapshot
# renders products from the rack's registry, and Tune is in neither.
if(BMO_BUILD_TUNE)
    add_subdirectory(tune)
endif()
