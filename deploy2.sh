#!/bin/bash

set -e

# cp /proc/$(pgrep -f run.sh | head -n 1)/cwd/main .
# sudo docker exec -it $(sudo docker ps -q) /bin/sh
# sudo docker cp $(sudo docker ps -q):main _deploy/main

sed -i 's/BC_DEPLOY=1/BC_DEPLOY=2/' _deploy/run.sh
