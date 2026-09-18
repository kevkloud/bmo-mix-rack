bmo_add_plugin(BmoDeq
    PRODUCT_NAME "BMO DEQ"
    PLUGIN_CODE  Bpar
    BUNDLE_ID    "com.lt3audio.bmodeq"
    CATEGORIES   "Fx" "EQ"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES    bmo_deq)
