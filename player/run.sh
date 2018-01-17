#!/bin/sh
# build the program!
# note: there will eventually be a separate build step for your bot, but for now it counts against your runtime.

# we provide this env variable for you
if [ "$BC_PLATFORM" = 'LINUX' ]; then
    LIBRARIES="-lbattlecode-linux -lutil -ldl -lrt -pthread -lgcc_s -lc -lm -L../battlecode/c/lib"
    INCLUDES="-I../battlecode/c/include -I."
elif [ "$BC_PLATFORM" = 'DARWIN' ]; then
    LIBRARIES="-lbattlecode-darwin -lSystem -lresolv -lc -lm -L../battlecode/c/lib"
    INCLUDES="-I../battlecode/c/include -I."
else
    echo "Unknown platform '$BC_PLATFORM' or platform not set"
    echo "Make sure the BC_PLATFORM environment variable is set"
    exit 1
fi

BC_DEPLOY=0
if [ "$BC_DEPLOY" = '2' ]; then
    # We run out of memory with g++. Instead we ship a pre-compiled binary.
    echo running chmod
    chmod +x main
elif [ "$BC_DEPLOY" = '1' ]; then
    echo g++ -std=c++11 -O2 -g -rdynamic everything.cpp -DCUSTOM_BACKTRACE -fno-omit-frame-pointer -no-pie -o main $LIBRARIES $INCLUDES
    g++ -std=c++11 -O2 -g -rdynamic everything.cpp -DCUSTOM_BACKTRACE -fno-omit-frame-pointer -no-pie -o main $LIBRARIES $INCLUDES
else
    echo g++ -std=c++11 -O2 -Wall -g -rdynamic everything.cpp -DBACKTRACE -o main $LIBRARIES $INCLUDES
    g++ -std=c++11 -O2 -Wall -g -rdynamic everything.cpp -DBACKTRACE -o main $LIBRARIES $INCLUDES
fi

# run the program!
./main
