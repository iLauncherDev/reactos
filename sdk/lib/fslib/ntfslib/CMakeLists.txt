
add_library(ntfslib ntfslib.c)
target_compile_definitions(ntfslib PRIVATE _USRDLL)
add_dependencies(ntfslib psdk)
