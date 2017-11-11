# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------
# Copyright © 2011, libcorkipset authors
# All rights reserved.
#
# Please see the COPYING file in this distribution for license details.
# ----------------------------------------------------------------------

#-----------------------------------------------------------------------
# Build the test cases

add_c_test(test-assignment)
add_c_test(test-bdd)
add_c_test(test-ipmap)
add_c_test(test-ipset)
add_c_test(test-iterator)

#-----------------------------------------------------------------------
# Command-line tests

find_package(PythonInterp)

if (PYTHON_EXECUTABLE)
    configure_file(ccram ${CMAKE_BINARY_DIR}/ccram COPYONLY)
    file(GLOB_RECURSE TESTS "${CMAKE_CURRENT_SOURCE_DIR}/*.t")
    foreach(TEST ${TESTS})
        get_filename_component(TEST_NAME "${TEST}" NAME_WE)
        add_test(
            ${TEST_NAME}
            ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}
            ${CMAKE_BINARY_DIR}/ccram
                --python ${PYTHON_EXECUTABLE}
                --root ${CMAKE_SOURCE_DIR}
                --tests ${TEST}
        )
    endforeach(TEST)
else (PYTHON_EXECUTABLE)
    message(WARNING "Unable to find Python; skipping cram tests.")
endif (PYTHON_EXECUTABLE)
