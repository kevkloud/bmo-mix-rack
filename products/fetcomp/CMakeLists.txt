bmo_add_plugin(BmoFet
    PRODUCT_NAME "BMO FET"
    PLUGIN_CODE  Bfet
    BUNDLE_ID    "com.lt3audio.bmofet"
    CATEGORIES   "Fx" "Dynamics"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES    bmo_fetcomp)
