cmake_minimum_required(VERSION 3.22)
# The version every plugin reports to its host (JucePlugin_VersionString) and
# the one scripts/package.sh names the zip by. It said 1.0.0 from the first
# commit while five tags went by; the 0.2.4 tester build shipped calling
# itself 1.0.0. Keep it equal to the tag being built.
project(BMOMixRack VERSION 0.2.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The DSP of every module has no JUCE dependency, so its tests and the
# measurement tools build and run on a machine with no audio framework at all,
# including a CI container. Configure with -DBMO_DSP_ONLY=ON to skip the
# plugins and everything JUCE brings with them.
option(BMO_DSP_ONLY "Build only the JUCE-free DSP tests and measurement tools" OFF)

# BMO Tune RT lives in this repository and shares core/, but it is not a rack
# module: nothing of it is in products/rack/Registry.cpp or on the rack's link
# line. These two switches are how that stays true rather than being promised.
# Each side configures, builds and tests with the other off, so a dependency
# between them is a configure error instead of something a reviewer has to
# notice. CI builds both ways.
option(BMO_BUILD_RACK "Build the rack, its modules and its products" ON)
option(BMO_BUILD_TUNE "Build BMO Tune RT" ON)

if(NOT BMO_BUILD_RACK AND NOT BMO_BUILD_TUNE)
    message(FATAL_ERROR
        "Nothing to build: BMO_BUILD_RACK and BMO_BUILD_TUNE are both OFF.")
endif()

# Every file in the tree includes from the root: "core/...", "modules/eq/...".
set(BMO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

#==============================================================================
# JUCE and the plugin set-up shared by every product
#==============================================================================
if(NOT BMO_DSP_ONLY)
    set(BMO_FORMATS VST3 Standalone)

    if(APPLE)
        list(APPEND BMO_FORMATS AU)

        # Dev builds are native arm64. For a release universal binary,
        # configure with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
        if(NOT CMAKE_OSX_ARCHITECTURES)
            set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
        endif()

        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()

    # Only development builds install into the user's plugin folders. A
    # Release build is for packaging and must not clobber what is being tested.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(BMO_INSTALL_AFTER_BUILD FALSE)
    else()
        set(BMO_INSTALL_AFTER_BUILD TRUE)
    endif()

    if(MSVC)
        # Static runtime, so a plugin does not depend on a redistributable
        # being installed on the listener's machine.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_subdirectory(libs/JUCE)

    # The typefaces are licensed to individuals, not to the project, so the
    # .otf files live outside this repository and are never committed. The
    # build is pointed at wherever you keep them. First hit wins:
    #
    #   1. -DBMO_FONT_DIR=<path>  one-off; it then sticks in that build cache
    #   2. $ENV{BMO_FONT_DIR}     a shell or a CI environment
    #   3. .bmo-fontdir           this working copy's constant, gitignored,
    #                             written by scripts/set-font-dir.sh
    #   4. assets/fonts/          what CI decodes from repository secrets
    #
    # See assets/fonts/README.md.
    set(BMO_FONT_DIR "" CACHE PATH "Directory holding the licensed .otf files")

    if(BMO_FONT_DIR)
        set(bmo_font_dir "${BMO_FONT_DIR}")
        set(bmo_font_from "-DBMO_FONT_DIR")
    elseif(DEFINED ENV{BMO_FONT_DIR})
        set(bmo_font_dir "$ENV{BMO_FONT_DIR}")
        set(bmo_font_from "environment BMO_FONT_DIR")
    elseif(EXISTS ${BMO_ROOT}/.bmo-fontdir)
        file(READ ${BMO_ROOT}/.bmo-fontdir bmo_font_dir)
        string(STRIP "${bmo_font_dir}" bmo_font_dir)
        set(bmo_font_from ".bmo-fontdir")
    else()
        set(bmo_font_dir "${BMO_ROOT}/assets/fonts")
        set(bmo_font_from "default, no local font folder recorded")
    endif()

    # Accept a Windows path with backslashes, and resolve a relative one
    # against the repository root rather than against the build directory.
    file(TO_CMAKE_PATH "${bmo_font_dir}" bmo_font_dir)
    get_filename_component(bmo_font_dir "${bmo_font_dir}" ABSOLUTE BASE_DIR ${BMO_ROOT})

    set(BMO_FONTS
        ${bmo_font_dir}/TG-MinervaBlack-Black.otf
        ${bmo_font_dir}/TG-Blender.otf)

    foreach(font ${BMO_FONTS})
        if(NOT EXISTS ${font})
            get_filename_component(missing ${font} NAME)
            message(FATAL_ERROR
                "Missing licensed font: ${missing}\n"
                "  looked in:  ${bmo_font_dir}\n"
                "  chosen by:  ${bmo_font_from}\n"
                "The .otf files are not in this repository and never will be. "
                "Point this working copy at your own licensed-font folder, once:\n"
                "    scripts/set-font-dir.sh <path-to-your-font-folder>\n"
                "See assets/fonts/README.md.")
        endif()
    endforeach()

    message(STATUS "BMO fonts: ${bmo_font_dir} (${bmo_font_from})")

    # A local folder outside the repository is the supported route now. The
    # in-repo assets/fonts/ path still works, so CI is unaffected and is not
    # nagged about it.
    if(bmo_font_from STREQUAL "default, no local font folder recorded"
       AND NOT DEFINED ENV{CI})
        message(WARNING
            "Building from the in-repo assets/fonts/ copy.\n"
            "Keep your licensed faces in a folder of their own outside this "
            "repository, and record it once with:\n"
            "    scripts/set-font-dir.sh <path-to-your-font-folder>\n"
            "Nothing in this repository ever writes to that folder. The only "
            "reason to open it is to add a newly licensed face for a future "
            "build. See assets/fonts/README.md.")
    endif()

    juce_add_binary_data(BmoAssets SOURCES ${BMO_FONTS})
    set_target_properties(BmoAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # One plugin target per product. The manufacturer code is shared by the
    # whole suite and is an input to the VST3 and AU identity: changing it
    # would orphan every session that references any LT3 Audio plugin. Each
    # product's plugin code is equally permanent. See plans/ and AGENTS.md.
    function(bmo_add_plugin target)
        set(one PRODUCT_NAME PLUGIN_CODE BUNDLE_ID INSTALL_AFTER_BUILD)
        set(multi CATEGORIES SOURCES LIBRARIES)
        cmake_parse_arguments(ARG "" "${one}" "${multi}" ${ARGN})

        # A product may opt out of the development install, which is what
        # BMO Tune RT does while its builds are compared by ear against the
        # installed one. Everything else takes the suite's rule above.
        if(NOT DEFINED ARG_INSTALL_AFTER_BUILD)
            set(ARG_INSTALL_AFTER_BUILD ${BMO_INSTALL_AFTER_BUILD})
        endif()

        juce_add_plugin(${target}
            COMPANY_NAME                "LT3 Audio"
            BUNDLE_ID                   ${ARG_BUNDLE_ID}
            PLUGIN_MANUFACTURER_CODE    LT3a
            PLUGIN_CODE                 ${ARG_PLUGIN_CODE}
            FORMATS                     ${BMO_FORMATS}
            PRODUCT_NAME                ${ARG_PRODUCT_NAME}
            IS_SYNTH                    FALSE
            NEEDS_MIDI_INPUT            FALSE
            NEEDS_MIDI_OUTPUT           FALSE
            IS_MIDI_EFFECT              FALSE
            EDITOR_WANTS_KEYBOARD_FOCUS FALSE
            COPY_PLUGIN_AFTER_BUILD     ${ARG_INSTALL_AFTER_BUILD}
            VST3_CATEGORIES             ${ARG_CATEGORIES}
            AU_MAIN_TYPE                kAudioUnitType_Effect)

        target_sources(${target} PRIVATE ${ARG_SOURCES})

        target_compile_definitions(${target} PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_REPORT_APP_USAGE=0)

        target_link_libraries(${target}
            PRIVATE
                ${ARG_LIBRARIES}
                juce::juce_audio_utils
                juce::juce_dsp
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
    endfunction()

    # A plain executable that uses the shared code without being a plugin:
    # tests and the snapshot tool. Not juce_add_console_app, which forces
    # JUCE_STANDALONE_APPLICATION=1 and collides with the plugin targets.
    function(bmo_add_tool target)
        add_executable(${target} ${ARGN})
        target_compile_definitions(${target} PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            JUCE_REPORT_APP_USAGE=0)
        target_link_libraries(${target} PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_dsp
            juce::juce_events
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    endfunction()
endif()

# A JUCE-free executable: DSP tests and measurement tools.
function(bmo_add_dsp_tool target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE ${BMO_ROOT})
    if(NOT BMO_DSP_ONLY)
        target_link_libraries(${target} PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    endif()
endfunction()

enable_testing()

add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(products)
add_subdirectory(tools)
add_subdirectory(tests)
