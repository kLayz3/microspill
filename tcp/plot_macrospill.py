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
    a.set_title("Real-time ECL_IN({}) macrospill".format(i+1))
    a.set_xlabel(r"$t$ [s]", fontsize=14, loc='right')
    a.set_ylabel("Count", fontsize=14)

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
        fig.suptitle(
            parsed_json["timestamp"] + "\n" + 
            "Spill number: {}".format(parsed_json["spill_number"]) + "\n"
            "Spill duration: {:.2f}s".format(parsed_json["spill_duration"]/1e8)
        )
        for i in range(4):
            data = parsed_json["data"][i]
            a = axes[i]
            a.cla()
            a.set_title("Real-time: {} macrospill".format(data["name"]))
            xs = data["macro_x"]
            ys = data["macro_y"]
            bar_width = xs[1] - xs[0]
            a.bar(xs, ys, width=bar_width, edgecolor='black', color=colours[i], capstyle='round', alpha = 0.5, zorder=3)
            a.text(0.5, 0.2,
            "Counted: {:.1f}k\nOffspill counted: {:d}".format(sum(ys) / 1e3, data["offspill"]),
            ha='center', va='bottom', transform = a.transAxes,
            bbox=dict(facecolor='lightyellow', edgecolor='black', alpha=0.66, capstyle='round'),fontsize='medium', fontweight='semibold')

            a.grid(True, axis='y', alpha=0.6, zorder=2)
            a.spines['top'].set_visible(False) 
            a.spines['right'].set_visible(False) 

            a.set_xlabel(r"$t$ [s]", fontsize=14, loc='right')
            a.set_ylabel("Count", fontsize=14)
       
        plt.pause(0.1)
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
