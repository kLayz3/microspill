#!/usr/bin/python3

import os,sys
import matplotlib.pyplot as plt
import numpy as np
import copy
import re
from math import log10, exp
import math
from datetime import datetime
from math import floor,ceil
import array as arr
# For a list, e.g. [0,0,... 0, a1, a2, 0, a3, ... aN, 0, 0, ... 0]
# With bunch of trailing and leading 0's, slice them out but keep last remaining
# (possible) zero's on both sides.

epsilon = 0.30103
def llog10(x):
    if(x == 0):
        return 0
    else:
        return log10(x) + epsilon

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


# for y ticks, value 0 => 0, value 1 => log(2), value N = log(N)+log(2)
def yticks(y):
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
    return vals, names, minor_ticks

def main():
    axes = []
    fig, axs = plt.subplots(2,2, figsize=(16,11))
    axes = axs.flatten();
    for i in range(0,4):
        axes[i].set_title("Real-time ECL_IN({}) microspill".format(i+1))
        axes[i].set_xlabel("log(t)")
        axes[i].set_ylabel("Count")
    n0_left = 1
    n0_right = 0
    i=0
    bins = [200,200,200,200]
    minx = [0., 0., 0., 0.]
    maxx = [5., 5., 5., 5.]
    ecl_diff  = [0,0,0,0]
    counted   = [0,0,0,0]
    overflows = [0,0,0,0]
    elapsed_time_10ns = [0, 0, 0, 0]
    hist_name = ["ECL_IN(1)", "ECL_IN(2)", "ECL_IN(3)", "ECL_IN(4)"];
    
    
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
                
                bin_indices = []
                # Cut below ~20 ns range:
                for x in range(0, n0_right+4):
                    if(maxx[i]/bins[i] * (x + 0.5) - 8 > -7.6):
                        bin_indices.append(x)

                xs = [maxx[i]/bins[i] * (x + 0.5) - 8 for x in xs]
                ys = [llog10(y) for y in ys]
                
                elapsed_time = elapsed_time_10ns[i] / 1e8 # Clock wraps around every 42s.. spill isn't longer than that.
                a = axes[i]
                a.cla()
                a.set_title("Real-time: {} microspill".format(hist_name[i]))
                bar_width = xs[1] - xs[0];
                a.bar(xs,ys, width=bar_width, align='edge', edgecolor='black', color=colours[i], capstyle='round', zorder=3, label="Data")

                # Poisson expected fit: def poisson_log_expected(x, N0, T_total, nbins, M)
                N0 = counted[i]
                T_total = elapsed_time_10ns[i]
                nbins = bins[i]
                M = maxx[i]
                if(N0 > 10 and T_total > 10):
                    ps = [poisson_log_expected(index, N0, T_total, nbins, M) for index in bin_indices]
                    prediction_x = [maxx[i]/bins[i] * (x + 0.5) - 8 for x in bin_indices]
                    a.plot(prediction_x, ps, linestyle='--', color='navy', linewidth=2, zorder=4, label = "Ideal Poisson".format(N0*1e5/T_total))
                    a.fill_between(prediction_x, ps, color='navy', alpha=0.15, zorder=2)
                a.text(0.86, 0.7, 
                    "Counted: {:.1f}k\nTime: {:.1f}s\nOverflow: {:d}\nLost: {:d}".format(counted[i]/1e3, elapsed_time_10ns[i]/1e8, overflows[i], abs(ecl_diff[i] - counted[i])), 
                    ha='left', va='center', transform = a.transAxes,
                    bbox=dict(facecolor='lightyellow', edgecolor='black', alpha=0.66, capstyle='round'),fontsize='medium', fontweight='semibold')

                a.grid(True, axis='y', alpha=0.7, zorder=2) 
                major_ticks, major_tick_labels, minor_ticks = xticks(xs)
                a.set_xticks(major_ticks, major_tick_labels, fontsize=12)
                a.set_xticks(minor_ticks, minor = True)
                a.tick_params(axis='x', which='major', length=13, width=1.2, direction='out')
                a.tick_params(axis='x', which='minor', length=5, width=1, direction='out')

                major_ticks, major_tick_labels, minor_ticks = yticks(ys)
                a.set_yticks(major_ticks, major_tick_labels, fontsize=12)
                a.set_yticks(minor_ticks, minor = True)
                a.tick_params(axis='y', which='major', length=11, width=1.1, direction='in')
                a.tick_params(axis='y', which='minor', length=4, width=1, direction='in')
                a.spines['top'].set_visible(False) 
                a.spines['right'].set_visible(False) 
                a.legend()
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
                    elif(sub[0] == "MAX"):
                        continue
                    elif(sub[0] == "MAX_LOG"):
                        maxx[i] = float(sub[1])
                    elif(sub[0] == "ECL_DIFF"):
                        ecl_diff[i] = int(sub[1])
                    elif(sub[0] == "COUNTED"):
                        counted[i] = int(sub[1])
                    elif(sub[0] == "OVERFLOWS"):
                        overflows[i] = int(sub[1])
                    elif(sub[0] == "ELAPSED_TIME"):
                        elapsed_time_10ns[i] = int(sub[1])
                    else:
                        break;

    except KeyboardInterrupt:
        print("\nKeyboard interrupt received. Exiting...")

    plt.ioff()
    

if __name__ == "__main__":
    main()
