set(CMAKE_CXX_VISIBILITY_PRESET hidden)

FILE(GLOB fse_src *.h *.c)

add_library(pbc_fse STATIC ${fse_src})
SET_TARGET_PROPERTIES(pbc_fse PROPERTIES LINKER_LANGUAGE C)

disable_target_warnings(pbc_fse)
