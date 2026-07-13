file(READ "${SPV_INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hexLen)
math(EXPR _byteLen "${_hexLen} / 2")
string(REGEX REPLACE "(..)" "0x\\1," _bytes "${_hex}")

set(_wgsl "")
if (WGSL_INPUT AND EXISTS "${WGSL_INPUT}")
    file(READ "${WGSL_INPUT}" _wgsl)
endif ()

file(WRITE "${OUTPUT}"
"#pragma once\n"
"// Generated from ${SPV_INPUT} — do not edit.\n"
"inline constexpr unsigned char ${SYMBOL}[] = { ${_bytes} };\n"
"inline constexpr unsigned long ${SYMBOL}_size = ${_byteLen}u;\n"
"inline constexpr const char* ${SYMBOL}_wgsl = R\"VORTEXWGSL(\n${_wgsl})VORTEXWGSL\";\n")
