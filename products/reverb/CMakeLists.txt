bmo_add_plugin(BmoLinger
    PRODUCT_NAME "BMO Linger"
    PLUGIN_CODE  Brvb
    BUNDLE_ID    "com.lt3audio.bmolinger"
    CATEGORIES   "Fx" "Reverb"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES    bmo_reverb)
