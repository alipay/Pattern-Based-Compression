add_subdirectory(deps)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

FILE(GLOB pbc_src compress/*.cc train/*.cc compress-c.cc common/*.cc)
INCLUDE_DIRECTORIES(deps)
ADD_LIBRARY(pbc_lib STATIC ${pbc_src})
ADD_DEPENDENCIES(pbc_lib pbc_fse pbc_fsst)
SET_TARGET_PROPERTIES(pbc_lib PROPERTIES OUTPUT_NAME "pbc")
TARGET_LINK_LIBRARIES(pbc_lib -Wl,--start-group ${LIBRARIES} pbc_fse pbc_fsst -Wl,--end-group)

FILE(GLOB pbc_tools_src tools/pbc.cc)
ADD_EXECUTABLE(pbc ${pbc_tools_src})
ADD_DEPENDENCIES(pbc pbc_lib)
TARGET_LINK_LIBRARIES(pbc pbc_lib)

FILE(GLOB pbc_test_src unit_test/test_main.cc unit_test/test_pbc.cc)
ADD_EXECUTABLE(pbc_test ${pbc_test_src})
ADD_DEPENDENCIES(pbc_test pbc_lib)
TARGET_LINK_LIBRARIES(pbc_test -Wl,--start-group pbc_lib gtest gflags -Wl,--end-group)
ADD_TEST (NAME pbc_test COMMAND pbc_test --dataset_path=${CMAKE_SOURCE_DIR}/dataset/)
