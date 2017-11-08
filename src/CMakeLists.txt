# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------
# Copyright © 2011-2015, RedJack, LLC.
# All rights reserved.
#
# Please see the COPYING file in this distribution for license details.
# ----------------------------------------------------------------------

#-----------------------------------------------------------------------
# Build the library

# Update the VERSION_INFO property below according to the following rules (taken
# from [1]):
#
# VERSION_INFO = current:revision:age
#
#   1. Start with a VERSION of `0:0:0` for each shared library.
#   2. Update VERSION_INFO only immediately before a public release of your
#      software.  More frequent updates are unnecessary, and only guarantee that
#      the current interface number gets larger faster.
#   3. If the library source code has changed at all since the last update, then
#      increment `revision` (`c:r:a` becomes `c:r+1:a`).
#   4. If any interfaces have been added, removed, or changed since the last
#      update, increment `current`, and set `revision` to 0.
#   5. If any interfaces have been added since the last public release, then
#      increment `age`.
#   6. If any interfaces have been removed or changed since the last public
#      release, then set `age` to 0.
#
# Note that changing `current` and setting `age` to 0 means that you are
# releasing a new backwards-incompatible version of the library.  This has
# implications on packaging, so once an API has stabilized, this should be a
# rare occurrence.
#
# [1] http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html#Updating-version-info

add_c_library(
    libipset
    OUTPUT_NAME ipset
    PKGCONFIG_NAME ipset
    VERSION_INFO 2:0:1
    SOURCES
        libipset/general.c
        libipset/bdd/assignments.c
        libipset/bdd/basics.c
        libipset/bdd/bdd-iterator.c
        libipset/bdd/expanded.c
        libipset/bdd/reachable.c
        libipset/bdd/read.c
        libipset/bdd/write.c
        libipset/map/allocation.c
        libipset/map/inspection.c
        libipset/map/ipv4_map.c
        libipset/map/ipv6_map.c
        libipset/map/storage.c
        libipset/set/allocation.c
        libipset/set/inspection.c
        libipset/set/ipv4_set.c
        libipset/set/ipv6_set.c
        libipset/set/iterator.c
        libipset/set/storage.c
    LIBRARIES
        libcork
)

#-----------------------------------------------------------------------
# Utility commands

add_c_executable(
    ipsetbuild
    OUTPUT_NAME ipsetbuild
    SOURCES ipsetbuild/ipsetbuild.c
    LOCAL_LIBRARIES
        libipset
)

add_c_executable(
    ipsetcat
    OUTPUT_NAME ipsetcat
    SOURCES ipsetcat/ipsetcat.c
    LOCAL_LIBRARIES
        libipset
)

add_c_executable(
    ipsetdot
    OUTPUT_NAME ipsetdot
    SOURCES ipsetdot/ipsetdot.c
    LOCAL_LIBRARIES
        libipset
)
