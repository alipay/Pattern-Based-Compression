function(disable_target_warnings NAME)
    if(MSVC)
      target_compile_options(${NAME} PRIVATE "/W0")
    elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang$"
           OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
      target_compile_options(${NAME} PRIVATE "-w")
    endif()
endfunction()

add_subdirectory(fse)
add_subdirectory(fsst)
