#!/bin/sh
# build the program!

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

DEPLOY_CC='g++ -std=c++11 -O2 -g -rdynamic -DCUSTOM_BACKTRACE -fno-omit-frame-pointer -no-pie'

BC_DEPLOY=0
if [ "$BC_DEPLOY" = '2' ]; then
    # We run out of memory with g++. Instead we ship a pre-compiled binary, and just link it in this step.
    echo $DEPLOY_CC everything.o -o main $LIBRARIES
    $DEPLOY_CC everything.o -o main $LIBRARIES
    echo starting
elif [ "$BC_DEPLOY" = '1' ]; then
    echo $DEPLOY_CC everything.cpp -c $INCLUDES
    $DEPLOY_CC everything.cpp -c $INCLUDES
    echo $DEPLOY_CC everything.o -o main $LIBRARIES
    $DEPLOY_CC everything.o -o main $LIBRARIES
else
    echo g++ -std=c++11 -O2 -Wall -g -rdynamic everything.cpp -DBACKTRACE -o main $LIBRARIES $INCLUDES
    g++ -std=c++11 -O2 -Wall -g -rdynamic everything.cpp -fsanitize=address -o main $LIBRARIES $INCLUDES
fi