add_library(toolchest_recovered OBJECT
    recovered/MenuParser.c
    recovered/Actions.c
)

# This target intentionally contains only binary-recovered Toolchest bodies.
# It is not named `toolchest` yet: the executable target is added only after
# the parser/menu/application state needed by the real IRIX entrypoint is
# reconstructed, rather than linking placeholder implementations.
set_target_properties(toolchest_recovered PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
