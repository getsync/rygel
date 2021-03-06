# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

PKG_CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D__STDC_FORMAT_MACROS -DLIBDRD_NO_WREN -Wstrict-overflow=0
PKG_CFLAGS += -std=gnu99
PKG_CXXFLAGS += -std=gnu++17 -Wno-class-memaccess

all:
	@mkdir -p ../R
	cp ../drdR_mco.R ../R/drdR_mco.R

override OBJECTS = $(addprefix ./${R_ARCH}_, drdR_mco.cc.o Rcc.cc.o libcc.cc.o libdrd.cc.o miniz.c.o)

./${R_ARCH}_drdR_mco.cc.o: ../drdR_mco.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_Rcc.cc.o: ../../../core/libwrap/Rcc.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_libcc.cc.o: ../../../core/libcc/libcc.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_libdrd.cc.o: ../../libdrd/libdrd.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<

MINIZ_FLAGS = -fno-strict-aliasing -DMINIZ_NO_MALLOC -DMINIZ_NO_STDIO -DMINIZ_NO_TIME\
              -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_ZLIB_APIS\
              -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
./${R_ARCH}_miniz.c.o: ../../../../vendor/miniz/miniz.c
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) $(MINIZ_FLAGS) -c -o $@ $<
