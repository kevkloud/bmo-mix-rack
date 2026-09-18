# The shared layer. An INTERFACE library with sources: every product compiles
# it into itself, because a static library would carry one product's
# JucePlugin_* definitions into the next.
add_library(bmo_core INTERFACE)
target_include_directories(bmo_core INTERFACE ${BMO_ROOT})

if(NOT BMO_DSP_ONLY)
    target_sources(bmo_core INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/state/PresetManager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Tokens.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Fonts.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/LookAndFeel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Controls.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/Line.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/ModulePanel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/PresetBar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/product/SingleModuleProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/product/ProductEditor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rack/RackProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rack/RackEditor.cpp)

    target_link_libraries(bmo_core INTERFACE BmoAssets)
endif()
