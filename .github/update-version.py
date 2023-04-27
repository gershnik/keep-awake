import argparse
import json
import sys
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('type', choices=['minor', 'major'])
parser.add_argument('--dry-run', dest='dryRun', action='store_true')
args = parser.parse_args()

mydir = Path(__file__).parent

def updateVersion(major, minor):
    if args.type == 'minor':
        minor = int(minor) + 1
    else:
        major = int(major) + 1
        minor = 0
    return (major, minor)


with open('version.json', 'r') as verfile:
    current = json.load(verfile)


major, minor = updateVersion(current["BUILD_MAJOR_VERSION"], current["BUILD_MINOR_VERSION"])
current["BUILD_MAJOR_VERSION"] = major
current["BUILD_MINOR_VERSION"] = minor


if not args.dryRun:
    with open('version.json', 'w') as verfile:
        json.dump(current, verfile, indent=4)


print(f'{major}.{minor}')


