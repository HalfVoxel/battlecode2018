#!/usr/bin/env python3
import json
import sys
j = json.load(open(sys.argv[1]))
with open('earth.txt', 'w') as f:
    print(j['earth'], file=f)
with open('mars.txt', 'w') as f:
    print(j['mars'], file=f)
print("wrote logs to earth.txt and mars.txt")
