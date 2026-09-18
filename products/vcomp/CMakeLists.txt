bmo_add_plugin(LtvComp
    PRODUCT_NAME "LTV Comp"
    PLUGIN_CODE  Ltvc
    BUNDLE_ID    "com.lt3audio.ltvcomp"
    CATEGORIES   "Fx" "Dynamics"
    SOURCES      ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    LIBRARIES    bmo_vcomp)
