#!/usr/bin/python3

import os,sys
import matplotlib.pyplot as plt
import numpy as np
import re
from math import log10, exp
import math
from datetime import datetime
from math import floor,ceil

if '--help' in sys.argv:
    print("Usage: {}\n\t--small\tPrints smaller sized plots")
    exit

colours = ["tomato", "greenyellow", "skyblue", "magenta"]

times = []
curr_time = 0
values = [[0], [0], [0], [0]]

axes = []
figsize = (16,11)

if '--small' in sys.argv:
    figsize=(10,7)
fig, axs = plt.subplots(2,2, figsize=figsize)
axes = axs.flatten();

xscale = 1.0
yscale = [1000,1000,1000,1000]

for i in range(0,4):
    a = axes[i]
    a.set_title("Real-time ECL_IN({}) macrospill".format(i+1))
    a.set_xlabel(r"$t$", fontsize=15, loc='right')
    a.set_ylabel("Count", fontsize=13)
    a.spines['top'].set_visible(False) 
    a.spines['right'].set_visible(False)

spill_time = 0.0

xdata = [0]
ydata = [[0], [0], [0], [0]]

offspill_counted = [0,0,0,0]
curr_time = 0

try:
    for line in sys.stdin:
        line = line.split()   
        if(len(line) < 2):
            continue
        if(line[0] != '--MACRO'):
            continue

        if(line[1] == 'RESET'):
            curr_time = 0
            xdata = [0]
            ydata = [[0], [0], [0], [0]]

        elif(line[1] == 'EOS'):
            for sub in line[3:]:
                sub = sub.split(":")
                i = int(sub[1]) - 1
                offspill_counted[i] = int(sub[3])
            
            for i in range(4):
                a = axes[i]
                a.cla()
                a.plot(xdata, ydata[i], color='black', linewidth=2.8, zorder=2)
                a.fill_between(xdata, ydata[i], color=colours[i], alpha=0.4, zorder=3)
                a.text(0.5, 0.2, 
                "Counted: {:.1f}k\nTime: {:.2f}s\nOffspill counted: {:d}".format(sum(ydata[i]) / 1e3, xdata[-1], offspill_counted[i]), 
                ha='center', va='bottom', transform = a.transAxes,
                bbox=dict(facecolor='lightyellow', edgecolor='black', alpha=0.66, capstyle='round'),fontsize='medium', fontweight='semibold')


                a.set_title("Real-time ECL_IN({}) macrospill".format(i+1))
                a.set_xlabel(r"$t\,[s]$", fontsize=16, loc='right')
                a.set_ylabel("Count", fontsize=15)
                a.tick_params(axis='both', which='major', labelsize='medium')
                a.tick_params(axis='both', which='minor', labelsize='small')
                a.grid(True, axis='both', alpha=0.6, zorder=34)
                a.spines['top'].set_visible(False) 
                a.spines['right'].set_visible(False)
            plt.pause(0.2) 

        # Line with data.
        elif(line[1].split(':')[0] == 'ELAPSED_TIME'):
            curr_time += float(line[1].split(':')[1])
            xdata.append(curr_time)
            for sub in line[2:]:
                sub = sub.split(':')
                i = int(sub[1]) - 1
                counted = int(sub[3])
                ydata[i].append(counted)

except KeyboardInterrupt:
    print("\nKeyboard interrupt received. Exiting...")

plt.ioff()
