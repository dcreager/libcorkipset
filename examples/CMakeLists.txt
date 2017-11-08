# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------
# Copyright © 2012, RedJack, LLC.
# All rights reserved.
#
# Please see the LICENSE.txt file in this distribution for license
# details.
# ----------------------------------------------------------------------

#-----------------------------------------------------------------------
# Examples

add_c_executable(
    ipv4-set-size
    SKIP_INSTALL
    OUTPUT_NAME ipv4-set-size
    SOURCES ipv4-set-size.c
    LIBRARIES
        libcork
    LOCAL_LIBRARIES
        libipset
)
