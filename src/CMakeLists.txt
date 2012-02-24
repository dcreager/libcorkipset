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
target_link_libraries(libipset
    ${GLIB_LIBRARIES})

install(TARGETS libipset
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

#-----------------------------------------------------------------------
# Generate the pkg-config file

set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(ipset.pc.in ipset.pc)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ipset.pc
        DESTINATION lib/pkgconfig)
