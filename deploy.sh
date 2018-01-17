#!/bin/bash
rm -rf _deploy/
cp -rL player/ _deploy/
sed -i 's/BC_DEPLOY=0/BC_DEPLOY=1/' _deploy/run.sh
rm -f _deploy/main
