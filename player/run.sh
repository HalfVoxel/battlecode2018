#!/bin/sh
# build the program!

if [ "$NO_BUILD" = '1' ]; then
    echo "Skipping build"
else
    ./build.sh
fi

# run the program!
./main
