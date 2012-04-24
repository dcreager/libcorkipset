# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------
# Copyright © 2011, RedJack, LLC.
# All rights reserved.
#
# Please see the LICENSE.txt file in this distribution for license
# details.
# ----------------------------------------------------------------------

include_directories(../include)

#-----------------------------------------------------------------------
# Build the library

file(GLOB_RECURSE LIBIPSET_SRC libipset/*.c)

add_library(libipset SHARED ${LIBIPSET_SRC})
set_target_properties(libipset PROPERTIES
    COMPILE_DEFINITIONS G_LOG_DOMAIN="ipset"
    OUTPUT_NAME ipset
    SOVERSION 1.1.0)
target_link_libraries(libipset ${CORK_LIBRARIES})

install(TARGETS libipset DESTINATION lib)

#-----------------------------------------------------------------------
# Utility commands

file(GLOB_RECURSE IPSETBUILD_SRC ipsetbuild/*.c)
add_executable(ipsetbuild ${IPSETBUILD_SRC})
target_link_libraries(ipsetbuild libipset)
install(TARGETS ipsetbuild DESTINATION bin)

file(GLOB_RECURSE IPSETCAT_SRC ipsetcat/*.c)
add_executable(ipsetcat ${IPSETCAT_SRC})
target_link_libraries(ipsetcat libipset)
install(TARGETS ipsetcat DESTINATION bin)

file(GLOB_RECURSE IPSETDOT_SRC ipsetdot/*.c)
add_executable(ipsetdot ${IPSETDOT_SRC})
target_link_libraries(ipsetdot libipset)
install(TARGETS ipsetdot DESTINATION bin)

#-----------------------------------------------------------------------
# Generate the pkg-config file

set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(ipset.pc.in ipset.pc)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ipset.pc
        DESTINATION lib/pkgconfig)
