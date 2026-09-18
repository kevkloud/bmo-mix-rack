# BMO Tune RT as a plugin, on the suite's bmo_add_plugin like every other
# product. It is a product of this repository; it is not in the rack.
#
# The manufacturer code is the suite's and the plugin code is this product's.
# Both are inputs to the VST3 identity, so both are permanent: changing either
# orphans every session that references the plugin. Btun and
# com.lt3audio.bmotunert were allocated by Frosty on 2026-09-10.
#
# INSTALL_AFTER_BUILD FALSE, where the rack's products take the suite default:
# a Debug build installs itself over the user's plugin folder, and Tune's
# builds are still being compared by ear against the installed 0.1. Install it
# by hand from products/tune/BmoTuneRT_artefacts/<config>/VST3.
bmo_add_plugin(BmoTuneRT
    PRODUCT_NAME        "BMO Tune RT"
    PLUGIN_CODE         Btun
    BUNDLE_ID           "com.lt3audio.bmotunert"
    CATEGORIES          "Fx" "Pitch Shift"
    INSTALL_AFTER_BUILD FALSE
    SOURCES             ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES           bmo_tune)
