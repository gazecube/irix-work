add_library(4dwm_recovered OBJECT
    recovered/WmFunctionCore.c
)

# Do not produce a `4Dwm` executable until the SGI WmGlobalData/event/client
# core is typed and the real entrypoint can be linked. This object target is a
# compile gate for functions whose IRIX bodies are already complete.
set_target_properties(4dwm_recovered PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
