
include_directories(${CPPUTEST_INCLUDE_DIR})

function(add_unit_test test_name test_src_file uut_src_file link_libraries object_files_directory)
    set(test_bin_file test_${test_name})
    add_executable(${test_bin_file} ${test_src_file})
    target_link_libraries(${test_bin_file} ${link_libraries} ${CPPUTEST_LIB})
    add_dependencies(${test_bin_file} cpputest_library)

    set(test_commands "cd ${CMAKE_CURRENT_SOURCE_DIR} && ${CMAKE_CURRENT_BINARY_DIR}/${test_bin_file}")

    if(${ENABLE_TESTS_WITH_GCOV})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../${uut_src_file})
            set(test_commands "${test_commands} && gcov -b ${uut_src_file} --object-directory ${object_files_directory}")
        endif()
    endif()

    if(${ENABLE_TESTS_WITH_VALGRIND})
        set(test_commands "${test_commands} && valgrind --tool=memcheck --leak-check=full --suppressions=valgrind.suppressions --error-exitcode=1 ${CMAKE_CURRENT_BINARY_DIR}/${test_bin_file}")
    endif()

    add_test(${test_name} bash -c "${test_commands}")
endfunction()
