#!/bin/bash

set -e

sudo docker exec -it $(sudo docker ps -q) /bin/sh -c 'cp /proc/$(pgrep -f run.sh | head -n 1)/cwd/main .'
sudo docker cp $(sudo docker ps -q):main _deploy/main

HASH=$(git rev-parse HEAD)
sed -i "s/BC_DEPLOY=1/BC_DEPLOY=2; COMMIT_HASH=$HASH/" _deploy/run.sh
