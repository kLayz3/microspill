#!/bin/bash

source ../env.sh

$VULOM4_CTRL --addr=3 --clear-setup

$VULOM4_CTRL --addr=3 --config=$DAQ_PATH/x86l-8/vulom.trlo \
	triva \
	timing \
	pulser_test \
	beam_gate_mimic \
	2>&1
