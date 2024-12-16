#!/bin/bash
help() {
	echo "Usage: $0 server [[ port ]] [[ UCESB_OPT ]]"
}

[ $# -lt 1 ] && { echo "Supply a server argument."; help; exit 1; }

SERVER=$1
shift
PORT=6002
[ $# -gt 0 ] && { PORT=$1; }
shift

while true; do
	./microspill stream://$SERVER:$PORT \
		--ntuple=RAW,STRUCT,port=8004 \
		--allow-errors \
		$@
	sleep 2
done
