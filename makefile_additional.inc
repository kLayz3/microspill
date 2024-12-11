USE_LMD_INPUT=1

CXXFLAGS+= -DCONTROL_INCLUDE -std=c++11 -Igen_microspill
OBJS += microspill_user.o
DEPENDENCIES += microspill_user.cc mapping.hh common.hh
