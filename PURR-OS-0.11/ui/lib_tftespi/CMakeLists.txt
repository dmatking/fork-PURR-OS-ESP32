# TFT_eSPI removed — CYD now uses pure IDF ILI9341 driver (drv_display/display_ili9341.cpp).
# This component is kept as an empty stub so existing REQUIRES references do not break.
idf_component_register(INCLUDE_DIRS "." "Extensions")
