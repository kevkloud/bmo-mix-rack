if(BMO_DSP_ONLY)
    return()
endif()

if(BMO_BUILD_RACK)
    add_subdirectory(eq)
    add_subdirectory(sat)
    add_subdirectory(util)
    add_subdirectory(opto)
    add_subdirectory(dim)
    add_subdirectory(deq)
    add_subdirectory(vcomp)
    add_subdirectory(rack)
endif()

# In the repository, not in the rack: BMO Tune RT is a product of its own and
# appears in no rack chain. See modules/tune/AGENTS.md.
if(BMO_BUILD_TUNE)
    add_subdirectory(tune)
endif()
