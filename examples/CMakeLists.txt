# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------
# Copyright © 2012, RedJack, LLC.
# All rights reserved.
#
# Please see the LICENSE.txt file in this distribution for license
# details.
# ----------------------------------------------------------------------

include_directories(../include)

#-----------------------------------------------------------------------
# Examples

add_executable(contains contains.c)
target_link_libraries(contains ${CORK_LIBRARIES} libipset)

add_executable(ipv4-set-size ipv4-set-size.c)
target_link_libraries(ipv4-set-size ${CORK_LIBRARIES} libipset)
