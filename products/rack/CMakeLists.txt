bmo_add_plugin(BmoMixRack
    PRODUCT_NAME "BMO Mix Rack"
    PLUGIN_CODE  Brck
    BUNDLE_ID    "com.lt3audio.bmomixrack"
    CATEGORIES   "Fx"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/Registry.cpp
    LIBRARIES    bmo_eq bmo_sat bmo_util bmo_opto bmo_dim bmo_deq bmo_vcomp)
