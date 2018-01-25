#!/bin/bash

set -e

echo Creating _deploy directory...
rm -rf _deploy/
cp -rL player/ _deploy/

HASH=$(git rev-parse HEAD)
sed -i "s/BC_DEPLOY=0/BC_DEPLOY=1; COMMIT_HASH=$HASH/" _deploy/build.sh

rm -f _deploy/main _deploy/*.o

echo Entering docker...
sudo docker exec $(sudo docker ps -q) sh -c '
set -e
echo Enter nested docker...
docker run -v /:/oldroot battlebaby sh -c "
set -e
echo Compiling...
cd /oldroot/player/_deploy/
BC_PLATFORM=LINUX ./build.sh
"
'

sed -i "s/BC_DEPLOY=1/BC_DEPLOY=2/" _deploy/build.sh

echo Done! Please test and submit _deploy via the docker web UI.
