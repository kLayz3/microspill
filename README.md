## Spill studies at FRS  

This software package takes data from a running VME system composed of: MVLC, TRIVA, VULOM4 and optionally VETAR.
Look in the `daq/` directory.

### Prerequisites
- [UCESB](https://git.chalmers.se/expsubphys/ucesb.git)
	- ``git clone https://git.chalmers.se/expsubphys/ucesb.git ``
	- ``make -C ucesb empty && export UCESB_DIR=$(pwd)/ucesb``

Compile the unpacker and utility software by simply running `make -j`

Spawn the struct server by peeking into a running DAQ:

``
./microspill SERVER --server=RAW,STRUCT,port=8004
``

Preferrably use the MBS/lwroc stream server, or MBS remote event server, instead of transport server. 

### Utilities
Peek from a running STRUCT server with the `data_printer` program from utils. Pass `--help` to any executable for
usage description and tweaks.

The latter program is only really meant to be piped. For example, to plot just microspill locally:

``
./data_printer STRUCT_SERVER | ./utils/microspill.py
``

Both micro- and macro-
``
./data_printer STRUCT_SERVER | tee >(./utils/macrospill.py) | ./utils/microspill.py
``

Check that shebangs on top of Python scripts point to a working interpreter!
Also don't forget to `pip install` in case packages are missing.
