CC = g++
SRC=data_printer.cc
COLFILE=colour-coding.hh

STRUCT_HH = ext_struct.hh

TAR = $(patsubst %.cc, %, $(SRC))

CFLAGS = -std=c++14 -Wall -Wextra \
	-I../ -O3 \
	-I$(UCESB_DIR)/hbook \
	-pthread

LDFLAGS = -L$(UCESB_DIR)/hbook \
		  -Wl,-rpath $(UCESB_DIR)/hbook \
		  -lext_data_clnt \

LIBS = $(UCESB_DIR)/hbook/ext_data_client.o

all : $(TAR) symlink

$(TAR) : $(SRC) $(STRUCT_HH) $(COLFILE)
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(LIBS)

$(STRUCT_HH):
	../microspill --ntuple=RAW,STRUCT_HH,$(STRUCT_HH)

.PHONY: symlink
symlink:
	ln -sf $(CURDIR)/data_printer ../

.PHONY: clean
clean:
	rm -f $(TAR)

.PHONY: cclean
cclean:
	rm -f $(TAR) $(STRUCT_HH)
