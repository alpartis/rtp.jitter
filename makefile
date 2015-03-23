# makefile cheatsheet ...
#
# $@ : name of target (left of the colon)
# $? : out-of-date dependencies
# $< : 'first' dependency
# $* : matched target from wildcard
#
INC = -I.
CPP=g++
CPPFLAGS=-std=c++11 $(INC)
CPLINK=g++
LOPTS=
LIB=ar rcs
LIBS=
STDLIBS=
LDLIBS=-luuid

%.cpp:
	$(CPP) $(CXXFLAGS) $*.cpp

rtp_jitter.o:

clean:
	rm -f rtp_jitter.o

