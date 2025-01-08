#!/usr/bin/python3
import os,sys

if(len(sys.argv) < 2):
    print(".. No host argument supplied!")
    print(f"Usage: {sys.argv[0]} HOST [PORT] [OPTS] ... default port is 8888");
    print("\nOPTS:")
    print("  --small    Draw on a smaller sized figure.")
    print("  --verbose  Print some debug output.");
    quit()

if '--help' in sys.argv:
    print(f"Usage: {sys.argv[0]} HOST [PORT] [OPTS] ... default port is 8888");
    print("\nOPTS:")
    print("  --small    Draw on a smaller sized figure.")
    print("  --verbose  Print some debug output.");
    quit()

verbose = False;
if '-v' in sys.argv or '--verbose' in sys.argv:
    verbose = True;

from datetime import datetime
import zmq
import threading
import time

import json
import matplotlib.pyplot as plt

fresh_data = None
lock = threading.Lock()

'''
In some cases, the data coming in comes faster than the matplotlib can update the plots,
therefore stuff the receiver in a separate thread that keeps only the latest sample.
Main thread fetches it and sets the object back to `None`
'''

def receive_data():
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    host = sys.argv[1]
    port = 8888
    try:
        port = int(sys.argv[2])
    except Exception:
        pass

    socket.connect(f"tcp://{host}:{port}")
    socket.setsockopt_string(zmq.SUBSCRIBE, '')

    print(f"Listening to {host} on port {port}")
    global fresh_data, verbose

    while True:
        try:
            message = socket.recv_string()
            parsed_json = json.loads(message)
            if verbose:
                print("[THREAD 1] Fetched via network spill #{}".format(parsed_json["spill_number"]));
            with lock:
                fresh_data = parsed_json
            time.sleep(0.01)
        except Exception as e:
            print(f"[THREAD 1] Error in fetching data: {e}")

receive_thread = threading.Thread(target=receive_data, daemon=True)
receive_thread.start()
axes = []
figsize = (16,11)
if '--small' in sys.argv:
    figsize=(10,7)
fig, axs = plt.subplots(2,2, figsize=figsize)
axes = axs.flatten();
for i in range(4):
    a = axes[i]
    a.set_title("Real-time ECL_IN({}) microspill".format(i+1))
    a.set_xlabel(r"$\log(t)$", fontsize=12)
    a.set_ylabel("Count", fontsize=12)

colours = ["tomato", "greenyellow", "skyblue", "magenta"]

plt.ion()

while True:
    try:
        with lock:
            parsed_json = fresh_data
            fresh_data = None 
            if verbose and parsed_json is not None:
                print("[MAIN THR] Fetched fresh data!");
        if not parsed_json:
            time.sleep(0.05)
            continue

        if verbose:
            print("[MAIN THR] Attempting to draw spill: #{}".format(parsed_json["spill_number"]));
        fig.suptitle(parsed_json["timestamp"] + "\n" + "Spill number: {}".format(parsed_json["spill_number"]))
        for i in range(4):
            data = parsed_json["data"][i]
            a = axes[i]
            a.cla()
            a.set_title("Real-time: {} microspill".format(data["name"]))
            xs = data["binx"]
            ys = data["biny"]
            assert len(xs) == len(ys), f"Lists `xs` and `ys` got different lengths: {len(xs)}, {len(ys)}\n"
            bar_width = xs[1] - xs[0]
            px, py = data["poisson_x"], data["poisson_y"];

            N0 = data["counted"]
            T_total = data["elapsed_time_10ns"] if data["elapsed_time_10ns"] > 0 else parsed_json["spill_duration"] 
            a.bar(xs, ys, width=bar_width, edgecolor='black', color=colours[i], capstyle='round', zorder=3)
            if(len(px) > 1 and px[0] is not None):
                a.plot(px, py, linestyle='--', color='navy', linewidth=2.8, zorder=4, label = "Ideal Poisson {:.1f} kHz".format(N0*1e5/T_total))
                a.fill_between(px, py, color='navy', alpha=0.2, zorder=2)
                a.text(0.86, 0.7, "Counted: {:.1f}k\nTime: {:.2f}s\nOverflow: {:d}\nLost: {:d}"
                       .format(data["counted"]/1e3, T_total/1e8, data["overflows"], data["lost_hits"]), ha='left', va='center', transform = a.transAxes,
                        bbox=dict(facecolor='lightyellow', edgecolor='black', alpha=0.66, capstyle='round'),fontsize='medium', fontweight='semibold')        
            
            a.grid(True, axis='y', alpha=0.7, zorder=2)
            a.set_xticks(data["xticks_major"], labels=data["xticks_major_label"], fontsize=12)
            a.set_xticks(data["xticks_minor"], minor = True)
            a.tick_params(axis='x', which='major', length=12, width=1.2, direction='out')
            a.tick_params(axis='x', which='minor', length=5, width=1, direction='out')

            a.set_yticks(data["yticks_major"], labels=data["yticks_major_label"], fontsize=12)
            a.set_yticks(data["yticks_minor"], minor = True)
            a.tick_params(axis='y', which='major', length=10, width=1.1, direction='in')
            a.tick_params(axis='y', which='minor', length=4, width=1, direction='in')
            a.spines['top'].set_visible(False) 
            a.spines['right'].set_visible(False) 

            a.set_xlabel(r"$\Delta t$", fontsize=15, loc='right')
            a.set_ylabel("Count", fontsize=13)
            if(N0 > 10): a.legend()
        
        plt.pause(0.2)
        if verbose:
            print("[MAIN THR] Drew a spill: #{}".format(parsed_json["spill_number"]));
            print("[MAIN THR] Spill time: {}".format(parsed_json["timestamp"]))
            t= datetime.now().strftime("%a %b %d %Y %H:%M:%S")
            print(f"[MAIN THR] Draw  time: {t}")
        
        time.sleep(0.05)

    except KeyboardInterrupt:
        print("Interrupted by user.")
        break
    finally:
        plt.show()
