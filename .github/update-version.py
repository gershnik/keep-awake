import argparse
import json
import sys
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('type', choices=['minor', 'major', 'patch'])
parser.add_argument('--dry-run', dest='dryRun', action='store_true')
args = parser.parse_args()

mydir = Path(__file__).parent

def updateVersion(major, minor, patch):
    if args.type == 'patch':
        patch = int(patch) + 1
    elif args.type == 'minor':
        minor = int(minor) + 1
        patch = 0
    else:
        major = int(major) + 1
        minor = 0
        patch = 0
    return (major, minor, patch)


with open('version.json', 'r') as verfile:
    current = json.load(verfile)


major, minor, patch = updateVersion(current["BUILD_MAJOR_VERSION"], 
                                    current["BUILD_MINOR_VERSION"],
                                    current["BUILD_PATCH_VERSION"])
current["BUILD_MAJOR_VERSION"] = major
current["BUILD_MINOR_VERSION"] = minor
current["BUILD_PATCH_VERSION"] = patch


if not args.dryRun:
    with open('version.json', 'w') as verfile:
        json.dump(current, verfile, indent=4)


print(f'{major}.{minor}.{patch}')


