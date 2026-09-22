bmo_add_plugin(BmoDwell
    PRODUCT_NAME "BMO Dwell"
    PLUGIN_CODE  Bdly
    BUNDLE_ID    "com.lt3audio.bmodwell"
    CATEGORIES   "Fx" "Delay"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES    bmo_dwell)
