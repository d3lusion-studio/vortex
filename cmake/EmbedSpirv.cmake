file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hexLen)
math(EXPR _byteLen "${_hexLen} / 2")
string(REGEX REPLACE "(..)" "0x\\1," _bytes "${_hex}")

file(WRITE "${OUTPUT}"
"#pragma once\n"
"// Generated from ${INPUT} — do not edit.\n"
"inline constexpr unsigned char ${SYMBOL}[] = { ${_bytes} };\n"
"inline constexpr unsigned long ${SYMBOL}_size = ${_byteLen}u;\n")
