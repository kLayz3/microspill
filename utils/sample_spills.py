#!/usr/bin/python3

import os,sys
import matplotlib.pyplot as plt
import numpy as np
import re
from math import log10
from datetime import datetime
from math import floor,ceil

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

lookup_time_scale = {
        0 : "1 s",
        -1 : "100 ms",
        -2 : "10 ms",
        -3 : "1 ms",
        -4 : r"100 $\mu$s",
        -5 : r"10 $\mu$s",
        -6 : r"1 $\mu$s",
        -7 : "100 ns",
        -8 : "10 ns",
        -9 : "1 ns",
}

# xs is sorted.
def xticks(x):
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
    return vals, names, minor_ticks

def main():
    axes = []
    fig, axs = plt.subplots(2,2, figsize=(15,10))
    axes = axs.flatten();
    for i in range(0,4):
        axes[i].set_title("Real-time ECL_IN({}) microspill".format(i+1))
        axes[i].set_xlabel("log(t)")
        axes[i].set_ylabel("Count")
    n0_left = 1
    n0_right = 0
    i=0
    bins = [200,200,200,200]
    minx = [0,0,0,0]
    maxx = [5., 5., 5., 5.]
    elapsed_time_10ns = [0, 0, 0, 0]
    hist_name = ["ECL_IN(1)", "ECL_IN(2)", "ECL_IN(3)", "ECL_IN(4)"];
    
    epsilon = 0.30103
    def llog10(x):
        if(x == 0):
            return 0
        else:
            return log10(x) + epsilon

    colours = ["tomato", "greenyellow", "skyblue", "magenta"]
    plt.ion()
    
    try:
        for line in sys.stdin:
            line = line.split();
            
            if(re.fullmatch("^\d+\.?\d*:\d$", line[0]) != None):
                #axes[i].cla()
                xs = [int(point.split(':')[0]) for point in line] 
                ys = [int(point.split(':')[1]) for point in line]

                # Trim out leading and trailing entries that have no y-data.
                ys,n0_left,n0_right = splice_zeros(ys)
                xs = xs[n0_left-1 : n0_right+2]

                assert len(xs) == len(ys), "Problem with asserting lengths. XS = {}, YS = {}".format(len(xs), len(ys))
                assert len(xs) >= 2, "ECL_IN({}): Array length should be >=2, is: {}".format(i+1, len(xs))
                
                xs = [maxx[i]/bins[i] * (x + 0.5) - 8 for x in xs]
                ys = [llog10(y) for y in ys]
                
                elapsed_time = elapsed_time_10ns[i] / 1e8 # Clock wraps around every 42s.. spill isn't longer than that.
                a = axes[i]
                a.cla()
                a.set_title("Real-time: {} microspill".format(hist_name[i]))
                bar_width = xs[1] - xs[0];
                a.bar(xs,ys, width=bar_width, align='edge', edgecolor='black', color=colours[i], capstyle='round', zorder=3, label="Data")
                a.grid(True, axis='y', alpha=0.7, zorder=2) 
                major_ticks, major_tick_labels, minor_ticks = xticks(xs)
                a.set_xticks(major_ticks, major_tick_labels, fontsize=12)
                a.set_xticks(minor_ticks, minor = True)
                a.tick_params(axis='x', which='major', length=13, width=1.2, direction='out')
                a.tick_params(axis='x', which='minor', length=5, width=1, direction='out')

                plt.pause(0.1);
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
                        hist_name[i] = sub[1]
                    elif(sub[0] == "NBINS"):
                        bins[i] = int(sub[1])
                    elif(sub[0] == "MIN"):
                        minx[i] = float(sub[1])
                    elif(sub[0] == "MAX_LOG"):
                        maxx[i] = float(sub[1])
                    elif(sub[0] == "MAX"):
                        continue
                    elif(sub[0] == "ELAPSED_TIME"):
                        elapsed_time_10ns[i] = int(sub[1])
                    else:
                        break;

    except KeyboardInterrupt:
        print("\nKeyboard interrupt received. Exiting...")

    plt.ioff()
    

if __name__ == "__main__":
    main()
