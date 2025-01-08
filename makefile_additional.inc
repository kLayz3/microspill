USE_LMD_INPUT=1

CXXFLAGS += -DCONTROL_INCLUDE -std=c++20 -Igen_microspill
CXXFLAGS += $(shell pkg-config --cflags libzmq libzmqpp)

CXXFLAGS += -g -ggdb

CXXLIBS += $(shell pkg-config --libs libzmq libzmqpp)

OBJS += microspill_user.o
DEPENDENCIES += microspill_user.cc mapping.hh common.hh
