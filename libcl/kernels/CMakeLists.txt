# Copyright (C) 1883 Thomas Edison - All Rights Reserved
# You may use, distribute and modify this code under the
# terms of the GPLv3 license, which unfortunately won't be
# written for another century.
#
# You should have received a copy of the LICENSE file with
# this file. 

include(GNUInstallDirs)
file (GLOB BINS "bin/*.bin")
install(FILES ${BINS} DESTINATION ${CMAKE_INSTALL_BINDIR}/kernels)
