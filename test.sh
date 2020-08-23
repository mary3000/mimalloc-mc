#! /bin/bash

# usage:
# ./test.sh path/to/test [--bug #bug_number (turns on bug number #bug_number)]
# example:
#./test.sh test/alloc_free_mt --bug 2

path="$1"
options=$(sed -n '1p' "$1"/config.txt)

bug=""

POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    --bug)
    num="$2"
    ((num++))
    cflags=$(sed -n "$num"'p' "$path"/config.txt)
    bug=-D"$cflags"
    shift # past argument
    shift # past value
    ;;
    *)
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

(lldb ./genmc-private/src/genmc -o run -- "$options"  -- "$bug" -I mimalloc/include -I . "$1"/main.c) 2>&1 | tee "$1"/out.txt