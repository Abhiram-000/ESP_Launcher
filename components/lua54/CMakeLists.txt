# ESP-IDF component for Lua 5.4.8
# All Lua source files are listed explicitly so cmake-minimal mode works.
# The lua.c and luac.c stand-alone executables are intentionally excluded.
# liolib.c and loslib.c are excluded (no POSIX file system on bare metal;
# our custom fs.* bindings replace them).

idf_component_register(
    SRCS
        "src/lapi.c"
        "src/lauxlib.c"
        "src/lbaselib.c"
        "src/lcode.c"
        "src/lcorolib.c"
        "src/lctype.c"
        "src/ldblib.c"
        "src/ldebug.c"
        "src/ldo.c"
        "src/ldump.c"
        "src/lfunc.c"
        "src/lgc.c"
        "src/linit.c"
        "src/llex.c"
        "src/lmathlib.c"
        "src/lmem.c"
        "src/loadlib.c"
        "src/lobject.c"
        "src/lopcodes.c"
        "src/lparser.c"
        "src/lstate.c"
        "src/lstring.c"
        "src/lstrlib.c"
        "src/ltable.c"
        "src/ltablib.c"
        "src/ltm.c"
        "src/lundump.c"
        "src/lutf8lib.c"
        "src/lvm.c"
        "src/lzio.c"
    INCLUDE_DIRS "src"
    REQUIRES freertos heap
)

# Optimise for size; Lua's VM is already very fast
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -Wno-maybe-uninitialized
)
