#!/bin/sh
# build the program!

if [ "$NO_BUILD" = '1' ]; then
    echo "Skipping build"
else
    chmod +x build.sh
    ./build.sh
fi

# run the program!
./main
