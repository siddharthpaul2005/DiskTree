# Turns a file into a C++ header holding its bytes:
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DSYMBOL=<name> -P embed_file.cmake
# Produces:  inline constexpr unsigned char <SYMBOL>[] = {...};
#            inline constexpr std::size_t   <SYMBOL>_size = N;
# A byte array (not a string literal) because MSVC caps string literals at 64 KB.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# Break into lines of 32 bytes to keep the header editor-friendly
string(REGEX REPLACE "((0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,))" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}"
"// Generated from ${INPUT} - do not edit\n#pragma once\n#include <cstddef>\n"
"inline constexpr unsigned char ${SYMBOL}[] = {\n${bytes}\n};\n"
"inline constexpr std::size_t ${SYMBOL}_size = ${size};\n")
