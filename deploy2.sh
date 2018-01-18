#!/bin/bash

set -e

sudo docker exec -it $(sudo docker ps -q) /bin/sh -c 'cp /proc/$(pgrep -f run.sh | head -n 1)/cwd/everything.o .'
sudo docker cp $(sudo docker ps -q):everything.o _deploy/everything.o

sed -i "s/BC_DEPLOY=1/BC_DEPLOY=2/" _deploy/run.sh
