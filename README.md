## Spill studies at FRS  

This software package takes data from a running VME system composed of: MVLC, TRIVA, VULOM4 and optionally VETAR.
Look in the `daq/` directory.

### DAQ Setting up
Cable the corresponding signals that wish to be sampled to ECL inputs 1-4 of the VULOM.
Put the two pulses representing beginning-of-spill (BoS), and end-of-spill (EoS) signals into channels 7 and 8, respectively.

### Prerequisites
- [UCESB](https://git.chalmers.se/expsubphys/ucesb.git)
- ``git clone https://git.chalmers.se/expsubphys/ucesb.git ``
- ``export UCESB_DIR=$(pwd)/ucesb``
- [libzmqpp](https://github.com/zeromq/zmqpp)
- [C++ JSON](https://github.com/nlohmann/json)

Compile the unpacker and utility software with `make -j$(nproc)`

Spawn the TCP server by peeking into a running DAQ:

``
./microspill SERVER --json
``

Preferrably use the MBS/lwroc stream server, or MBS remote event server, instead of transport server. 
**Check extra utilities by passing `--help` to the executable.**

## Data Structure
To check the data structure, look into `tcp/example.json` . This file can always be regenerated using the `--json_dump` flag with a working DAQ.

### Microspill
Microspill data represents spectrum of timing differences between consecutive hits (rising edges of digital signals), as recognised by the VULOM, in units of nanoseconds.
Timing jitter is ~10 ns.
It is always given in **log-log** scale, with bins having equals width in log scale (therefore, unequal widths in linear scale).
The x- position of a bin will always be given by its *central* position, and the constant bin width is the difference between two consecutive bin positions.

If enough of the data is sampled in a channel, then two Poisson arrays will also be given (which are otherwise `null`).
This data represents how an ideal Poissonian distribution would look like, if the same number of hits were to be sampled, in the same amount of time.

### Macrospill
Macrospill data is also given, with the intial time 0 being given by the BoS signal. It is given in **lin-lin** scale.

#### JSON keys:
- `spill_duration`    - elapsed time between BoS and EoS received triggers, in units of 10 ns.
- `spill_number`      - spill counter, starts from 0 when the executable lauches.
- `timestamp`         - datetime string either derived from Whiterabbit (preferrable) or from unpacking machine's system time.
- `data`              - array of four JSON objects holding the individual channel's data

#### `j["data"]` object microspill:
- `name`               - name of the channel, can be passed via `--alias` flag to the server process.
- `offspill`           - offspill counts.
- `binx`               - arithmetic sequence (type: Number) of central positions of the bins.
- `biny`               - corresponding sequence of heights of each bin.
- `overflows`          - number of hits which have the timing difference greater than `max_range = 0.1s`
- `poisson_y`          - sequence of bin heights that a Poissonian distribution would have. Can be `null`.
- `poisson_x`          - corresponding central bin positions.
- `elapsed_time_10ns`  - elapsed time between the initial and the final hit during the on-spill.
- `lost_hits`          - hits that got counted by the scaler but didn't get stamped.
- `xticks_major`       - major x-axis ticks for the plot.
- `xticks_minor`       - minor x-axis ticks for the plot.
- `xticks_major_label` - labels of each of the major x-ticks.
- `yticks_major`       - major y-axis ticks for the plot.
- `yticks_minor`       - minor y-axis ticks for the plot.
- `yticks_major_label` - labels of each of the major y-ticks.

Additionally:
- `macro_x`            - arithmetic sequence (type: Number) of central positions of the bins.
- `macro_y`            - corresponding sequence of heights of each bin.
- `macro_errors`       - hits that are too far away in time from BoS, or are else somehow miscalculated.
- `offspill`           - amount of hits counted during offspill.

Examples of how to quickly draw the data using Python is given in `tcp/plot_*.py` programs.

## Utilities

Peek from a running JSON server with the `tcp/plot_*` program(s). Pass `--help` to any executable for
usage description and tweaks.

Check that shebangs on top of Python scripts point to a working interpreter.
Don't forget to `pip install` in case packages are missing!


## TODO's
- [] Users entering manually the `--max-range_i=??` for microspill.
- [] Trloii section to allow users to pass a full beam gate instead of edges to, say, channel 6.
- [] Trloii update to allow trigger types 12, 13 to be uniquely mapped to a different sequence, to not sample the 1st FIFO.
- [] Slap a GPL on this?

## Author
- [Martin Bajzek](https://github.com/kLayz3)
