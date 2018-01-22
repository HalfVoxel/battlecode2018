#!/bin/bash

set -e

rm -rf _deploy/
cp -rL player/ _deploy/

HASH=$(git rev-parse HEAD)
sed -i "s/BC_DEPLOY=0/BC_DEPLOY=1; COMMIT_HASH=$HASH/" _deploy/run.sh

rm -f _deploy/main _deploy/*.o
