#! /bin/bash

# usage:
# ./test.sh path/to/test [--bug (turns on buggy version)]

options=$(sed -n '1p' "$1"/config.txt)
cflags=$(sed -n '2p' "$1"/config.txt)

bug=""

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    --bug)
    bug=-D"$cflags"
    shift # past argument
    ;;
    *)
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

(./genmc-private/src/genmc "$options"  -- "$bug" -I mimalloc/include -I . "$1"/main.c) 2>&1 | tee "$1"/out.txt