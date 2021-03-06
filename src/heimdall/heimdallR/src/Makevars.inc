# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

PKG_CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D__STDC_FORMAT_MACROS -Wstrict-overflow=0
PKG_CFLAGS += -std=gnu99
PKG_CXXFLAGS += -std=gnu++17 -Wno-class-memaccess

all:
	@mkdir -p ../R
	cp ../heimdallR.R ../R/heimdallR.R

override OBJECTS := $(addprefix ./${R_ARCH}_, heimdallR.cc.o Rcc.cc.o libcc.cc.o libgui.cc.o opengl.cc.o libheimdall.cc.o\
                                              imgui.cpp.o imgui_demo.cpp.o imgui_draw.cpp.o imgui_widgets.cpp.o miniz.c.o\
                                              roboto_medium_ttf.c.o)

./${R_ARCH}_heimdallR.cc.o: ../heimdallR.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_Rcc.cc.o: ../../../core/libwrap/Rcc.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_libcc.cc.o: ../../../core/libcc/libcc.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_libgui.cc.o: ../../../core/libgui/libgui.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_opengl.cc.o: ../../../core/libwrap/opengl.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_libheimdall.cc.o: ../../libheimdall/libheimdall.cc
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_roboto_medium_ttf.c.o: ../../libheimdall/roboto_medium_ttf.c
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

./${R_ARCH}_imgui.cpp.o: ../../../../vendor/imgui/imgui.cpp
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_imgui_demo.cpp.o: ../../../../vendor/imgui/imgui_demo.cpp
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_imgui_draw.cpp.o: ../../../../vendor/imgui/imgui_draw.cpp
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<
./${R_ARCH}_imgui_widgets.cpp.o: ../../../../vendor/imgui/imgui_widgets.cpp
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<

MINIZ_FLAGS = -fno-strict-aliasing -DMINIZ_NO_MALLOC -DMINIZ_NO_STDIO -DMINIZ_NO_TIME\
              -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_ZLIB_APIS\
              -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
./${R_ARCH}_miniz.c.o: ../../../../vendor/miniz/miniz.c
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) $(MINIZ_FLAGS) -c -o $@ $<
