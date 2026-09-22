bmo_add_plugin(BmoDefang
    PRODUCT_NAME "BMO Defang"
    PLUGIN_CODE  Bdes
    BUNDLE_ID    "com.lt3audio.bmodefang"
    CATEGORIES   "Fx" "Dynamics"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES    bmo_deesser)
