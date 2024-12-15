#!/bin/bash
help() {
	echo "Usage: $0 server [[ port ]]"
}

[[ $# < 1 ]] && { help; exit 1; }

SERVER=$1
shift

PORT=6002
[[ $# > 0 ]] && { PORT=$1; }

while true; do
	./microspill stream://$SERVER:$PORT --ntuple=RAW,STRUCT,port=8004
	sleep 2
done
