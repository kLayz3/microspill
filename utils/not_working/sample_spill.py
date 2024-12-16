#!/usr/bin/python3

import os,sys
import queue,threading
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np
import copy
import re
import time
from math import log10, exp
import math
from datetime import datetime
from math import floor,ceil

stop_event = threading.Event()

class HistInfo:
    def __init__(self, 
            index = 0,
            name = "", nbins = 100, minx = 0.0, maxx = 5.0):
        self.index = index
        self.name = name
        self.nbins = nbins
        self.minx = minx
        self.maxx = maxx
    def __iter__(self):
        yield self.index 
        yield self.name 
        yield self.nbins 
        yield self.minx 
        yield self.maxx 

class Poisson:
    def __init__(self,
            xs = [], ys = [], label = ""):
        self.xs = xs
        self.ys = ys
        self.label = label
    def __iter__(self):
        yield self.xs 
        yield self.ys 
        yield self.label 

#counted[i]/1e3, elapsed_time_10ns[i]/1e8, overflows[i], abs(ecl_diff[i] - counted[i]
class SpillData:
    def __init__(self,
            counted = 0, 
            ecl_diff = 0,
            elapsed_time_10ns = 0,
            overflows = 0):
        self.counted = counted
        self.ecl_diff = ecl_diff
        self.elapsed_time_10ns = elapsed_time_10ns
        self.overflows = overflows
        self.lost = abs(counted - ecl_diff)
    def __iter__(self):
        yield self.counted 
        yield self.ecl_diff 
        yield self.elapsed_time_10ns 
        yield self.overflows 
        yield self.lost 

class PlotTicks:
    def __init__(self,
            major_ticks = [],
            names = [""], 
            minor_ticks = []):
        self.major_ticks = major_ticks
        self.names = names
        self.minor_ticks = minor_ticks
    def __iter__(self):
        yield self.major_ticks 
        yield self.names 
        yield self.minor_ticks 

class MicroOutput:
    def __init__(self, 
            hist_info = HistInfo(), 
            xs = [], ys = [],
            poiss = Poisson(),
            xticks = PlotTicks(), 
            yticks = PlotTicks(),
            spill_data = SpillData()):
        self.hist_info = hist_info
        self.xs = xs
        self.ys = ys
        self.poiss = poiss
        self.xticks = xticks
        self.yticks = yticks
        self.spill_data = spill_data
    def __iter__(self):
        yield self.hist_info 
        yield self.xs 
        yield self.ys 
        yield self.poiss 
        yield self.xticks 
        yield self.yticks 
        yield self.spill_data 

# Used to gracefully handle log(0) in histograms.
epsilon = 0.30103
def llog10(x):
    if(x == 0):
        return 0
    else:
        return log10(x) + epsilon

# For a list, e.g. [0,0,... 0, a1, a2, 0, a3, ... aN, 0, 0, ... 0]
# With bunch of trailing and leading 0's, slice them out but keep last remaining
# (possible) zero's on both sides.
def splice_zeros(l):
    n0_right = 0
    n0_left = 0
    for i in range(len(l)-1, -1, -1):
        if l[i] != 0:
            n0_right = i
            break
    for i in range(0, len(l)):
        if l[i] != 0:
            n0_left = i
            break
    
    n0_right = len(l) - 2 if n0_right+2 > len(l) else n0_right
    n0_left = 1 if n0_left == 0 else n0_left

    return (l[n0_left-1 : n0_right+2], n0_left, n0_right)

# `x` is the bin number. x in [0,1,2,3, ... nbins-1]
def poisson_log_expected(x, N0, T_total, nbins, M):
    if T_total == 0 or nbins == 0:
        return np.zeroes(len(x))

    f = N0 / T_total # `T_total` is in units of 10 ns
    val = np.log10(N0) + np.log10( 
        np.exp(-f * np.power(10, x * M / nbins)) - np.exp(-f * np.power(10, (x+1) * M / nbins))
    ) + epsilon
    return val if val > 0 else 0

lookup_time_scale = {
        0 : "1 s",
        -1 : "100 ms",
        -2 : "10 ms",
        -3 : "1 ms",
        -4 : r"100 $\mathrm{\mu}$s",
        -5 : r"10 $\mathrm{\mu}$s",
        -6 : r"1 $\mathrm{\mu}$s",
        -7 : "100 ns",
        -8 : "10 ns",
        -9 : "1 ns",
}

# xs is sorted.
def calc_xticks(x):
    minx = floor(x[0]);
    maxx = ceil(x[-1]); 
    vals = np.arange(minx, maxx+1);
    names = []
    minor_ticks = []
    for val in vals:
        names.append(lookup_time_scale.get(val, "Inf"))
    for val in vals[:-1]:
        minor_ticks.append([val + log10(n) for n in range(2,10)])
    
    minor_ticks = [x for sublist in minor_ticks for x in sublist]
    return PlotTicks(vals, names, minor_ticks)


# for y ticks, value 0 => 0, value 1 => log(2), value N = log(N)+log(2)
def calc_yticks(y):
    minx = 0    
    maxx = max(y)*1.08
    vals = np.arange(minx, floor(maxx)+1) + epsilon;
    names = []
    minor_ticks = []
    for val in vals:
        names.append(r"$10^{:d}$".format(floor(val)));
    for val in vals[:-1]:
        minor_ticks.append([val + log10(n) for n in range(2,10)])
    minor_ticks = [x for sublist in minor_ticks for x in sublist]
    n=2;
    while(vals[-1] + log10(n) < maxx):
        minor_ticks.append( vals[-1] + log10(n))
        n += 1
    return PlotTicks(vals, names, minor_ticks)

def read_stdin(queue_micro, queue_macro):
    while True and not stop_event.is_set():
        line = sys.stdin.readline().split() 
        if(len(line) == 0):
            continue

        if(line[0] == "--MACRO"):
            queue_macro.put(line)
        else:
            queue_micro.put(line)


def handle_microspill(queue_input, queue_output):
    i=0
    hist_info,spill_data = [],[]
    for i in range(4):
        h0 = HistInfo(i, "ECL_IN({:d})".format(i), 200, 0., 5.)
        s0 = SpillData(0,0,0,0)
        hist_info.append(h0)
        spill_data.append(s0)
    
    try:
        while True and not stop_event.is_set():
            while not queue_input.empty():
                line = queue_input.get()
                if(re.fullmatch("^\d+\.?\d*:\d$", line[0]) != None):

                    # Object to be passed into the output queue, back to the main thread.
                    output = MicroOutput(
                            hist_info=hist_info[i],
                            spill_data=spill_data[i])
                    xs = [int(point.split(':')[0]) for point in line] 
                    ys = [int(point.split(':')[1]) for point in line]

                    # Trim out leading and trailing entries that have no y-data.
                    ys,n0_left,n0_right = splice_zeros(ys)
                    xs = xs[n0_left-1 : n0_right+2]

                    assert len(xs) == len(ys), "Problem with asserting lengths. XS = {}, YS = {}".format(len(xs), len(ys))
                    assert len(xs) >= 2, "ECL_IN({}): Array length should be >=2, is: {}".format(i+1, len(xs))
                    
                    bin_indices = []
                    # Cut below ~20 ns range:
                    maxx, nbins = hist_info[i].maxx, hist_info[i].nbins
                    for x in range(0, n0_right+4):
                        if(maxx/nbins * (x + 0.5) - 8 > -7.6):
                            bin_indices.append(x)

                    xs = [maxx/nbins * (x + 0.5) - 8 for x in xs]
                    ys = [llog10(y) for y in ys]
                    output.xs, output.ys = xs, ys
                    
                    # Poisson expected fit
                    N0 = spill_data[i].counted
                    T_total = spill_data[i].elapsed_time_10ns
                    nbins = hist_info[i].nbins
                    M = hist_info[i].maxx

                    if(N0 > 10 and T_total > 10):
                        ps = [poisson_log_expected(index, N0, T_total, nbins, M) for index in bin_indices]
                        prediction_x = [maxx/nbins * (x + 0.5) - 8 for x in bin_indices]
                        output.poiss = Poisson(xs = prediction_x, ys = ps, label = "Ideal Poisson".format(N0*1e5/T_total))
                    
                    if(len(xs) > 5):
                        output.xticks = calc_xticks(xs)
                        output.yticks = calc_yticks(ys)
                    if(N0 > 10):
                        print("Microthread: Put in output.")
                        queue_output.put(output)
                else:
                    for sub in line:
                        sub = sub.split(":")
                        if(sub[0] == "TS"):
                            ns = int(sub[1])
                            s_stamp = ns // 1_000_000_000
                            cs = (ns // 1_000_000_0) % 100
                            dt = datetime.fromtimestamp(s_stamp)
                            formatted = dt.strftime('%a %b %d %Y %H:%M:%S')
                            formatted += ".{:2d}".format(cs)
                        elif(sub[0] == "INDEX" ):
                            i = int(sub[1]) - 1
                        elif(sub[0] == "HIST"):
                            if(sub[1].isdigit):
                                sub[1] = "ECL_IN(" + sub[1] + ")"
                            hist_info[i].hist_name = sub[1]
                        elif(sub[0] == "NBINS"):
                            hist_info[i].nbins = int(sub[1])
                        elif(sub[0] == "MIN"):
                            hist_info[i].minx = float(sub[1])
                        elif(sub[0] == "MAX"):
                            continue
                        elif(sub[0] == "MAX_LOG"):
                            hist_info[i].maxx = float(sub[1])
                        elif(sub[0] == "ECL_DIFF"):
                            spill_data[i].ecl_diff = int(sub[1])
                        elif(sub[0] == "COUNTED"):
                            spill_data[i].counted = int(sub[1])
                        elif(sub[0] == "OVERFLOWS"):
                            spill_data[i].overflows = int(sub[1])
                        elif(sub[0] == "ELAPSED_TIME"):
                            spill_data[i].elapsed_time_10ns = int(sub[1])
                        else:
                            break;
    except KeyboardInterrupt:
        return

def handle_macrospill(queue):
    try:
        while True and not stop_event.is_set():
            while not queue.empty():
                line = queue.get() # It's already split as a list.
    except KeyboardInterrupt:
        return

def main():
    try:
        start_time = time.time()
        queue_micro  = queue.Queue(maxsize = 1000) 
        queue_macro  = queue.Queue(maxsize = 1000) 
        micro_output = queue.Queue(maxsize = 100)

        # Thread to read from STDIN, distribute to the macro/micro queue.
        thread_reader = threading.Thread(target=read_stdin, args=(queue_micro, queue_macro), daemon=True)

        # Thread to fetch microspill data, digest and give back via `micro_output` to main.
        thread_micro  = threading.Thread(target=handle_microspill, args=(queue_micro, micro_output), daemon=True)

        # Thread to fetch macrospill data, digest and give back via `macro_output` to main.
        thread_macro  = threading.Thread(target=handle_macrospill, args=(queue_macro,), daemon=True)

        thread_reader.start()
        thread_micro.start()
        thread_macro.start()

        ### Stuff for microspill. ###
        axes = []
        fig, axs = plt.subplots(2,2, figsize=(16,11))
        axes = axs.flatten();
        for i in range(0,4):
            axes[i].set_title("Real-time ECL_IN({}) microspill".format(i+1))
            axes[i].set_xlabel("log(t)")
            axes[i].set_ylabel("Count")
            axes[i].spines['top'].set_visible(False) 
            axes[i].spines['right'].set_visible(False) 
            axes[i].grid(True, axis='y', alpha=0.7, zorder=2) 
            axes[i].tick_params(axis='x', which='major', length=13, width=1.2, direction='out')
            axes[i].tick_params(axis='x', which='minor', length=5, width=1, direction='out')
            axes[i].tick_params(axis='y', which='major', length=11, width=1.1, direction='in')
            axes[i].tick_params(axis='y', which='minor', length=4, width=1, direction='in')

        colours = ["tomato", "greenyellow", "skyblue", "magenta"]
        
        plt.ion()
        
        while True:
            try:
                while not micro_output.empty():
                    struct = micro_output.get()
                    print("-- UPDATE: ", datetime.now())
                    # Decompose it back for easier naming.
                    # Isn't nice, but f*** it.
                    (
                        (i,hist_name,nbins,minx,maxx), 
                        xs,ys, 
                        (pxs, pys, plabel),
                        (xticks_major, xticks_names, xticks_minor), 
                        (yticks_major, yticks_names, yticks_minor),
                        (counted, ecl_diff, elapsed_time_10ns, overflows, lost)
                    ) = struct
                    
                    if(len(pxs) > 0):
                        a = axes[i] 
                        a.cla()
                        a.set_title("Real_time: {} microspill".format(hist_name))
                        bar_width = xs[1] - xs[0]
                        a.bar(xs,ys, width=bar_width, align='edge', edgecolor='black', color=colours[i], capstyle='round', zorder=3, label="Data")
                        a.plot(pxs, pys, linestyle='--', color='navy', linewidth=2.8, zorder=4, label = plabel)
                        a.fill_between(pxs, pys, color='navy', alpha=0.2, zorder=2)
                        a.text(0.86, 0.7, 
                        "Counted: {:.1f}k\nTime: {:.2f}s\nOverflow: {:d}\nLost: {:d}".format(counted, elapsed_time_10ns/1e8, overflows, abs(ecl_diff - counted)), 
                        ha='left', va='center', transform = a.transAxes, 
                        bbox=dict(facecolor='lightyellow', edgecolor='black', alpha=0.66, capstyle='round'),fontsize='medium', fontweight='semibold')
                        a.set_xticks(xticks_major, xticks_names, fontsize=12)
                        a.set_xticks(xticks_minor, minor = True)

                        a.set_yticks(yticks_major, yticks_names, fontsize=12)
                        a.set_yticks(yticks_minor, minor = True)
                        a.legend()
                        
                        plt.pause(0.2);

            except KeyboardInterrupt:
                print("\n{}: main loop. Keyboard interrupt received. Freezing plots. Press again to terminate.".format(sys.argv[0]))
                stop_event.set()
                break

        thread_reader.join()
        thread_micro.join()
        thread_macro.join()
        print("{}: all threads finished. Quitting".format(sys.argv[0]))
    
    except KeyboardInterrupt:
        print("\n{}: second keyboard interrupt received. Exiting..\n".format(sys.argv[0]))

    plt.ioff()

if __name__ == "__main__":
    main()















