#!/bin/sh
# build the program!

set -e

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

step() {
    echo $@
    $@
}

DEPLOY_CC='g++ -std=c++11 -O2 -g -rdynamic -DNDEBUG -DCUSTOM_BACKTRACE -fno-omit-frame-pointer -no-pie'

BC_DEPLOY=0
if [ "$BC_DEPLOY" = '2' ]; then
    # We run out of memory with g++. Instead we ship a pre-compiled binary, and just link it in this step.
    step $DEPLOY_CC everything.o -o main $LIBRARIES
    echo starting $COMMIT_HASH
elif [ "$BC_DEPLOY" = '1' ]; then
    step $DEPLOY_CC everything.cpp -c $INCLUDES
else
    if [ "$BC_PLATFORM" = 'DARWIN' ]; then
        step g++ -std=c++11 -O2 -Wall -g -rdynamic everything.cpp -DBACKTRACE -o main $LIBRARIES $INCLUDES
    else
        step g++ -std=c++11 -O2 -Wall -g -rdynamic everything.cpp -DCUSTOM_BACKTRACE -fno-omit-frame-pointer -no-pie -o main $LIBRARIES $INCLUDES
    fi
fi
