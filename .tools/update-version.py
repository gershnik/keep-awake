import argparse
import json
import sys
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('type', choices=['minor', 'major', 'patch'])
args = parser.parse_args()

mydir = Path(__file__).parent

def nextVersion(major, minor, patch):
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


major, minor, patch = nextVersion(current["BUILD_MAJOR_VERSION"], 
                                  current["BUILD_MINOR_VERSION"],
                                  current["BUILD_PATCH_VERSION"])

NEW_VER = f'{major}.{minor}.{patch}'
print(f'Version: {NEW_VER}')

current["BUILD_MAJOR_VERSION"] = major
current["BUILD_MINOR_VERSION"] = minor
current["BUILD_PATCH_VERSION"] = patch


with open('version.json', 'w') as verfile:
    json.dump(current, verfile, indent=4)



unreleased_link_pattern = re.compile(r"^\[Unreleased\]: (.*)$", re.DOTALL)
lines = []
with open(ROOT / "CHANGELOG.md", "rt") as change_log:
    for line in change_log.readlines():
        # Move Unreleased section to new version
        if re.fullmatch(r"^## Unreleased.*$", line, re.DOTALL):
            lines.append(line)
            lines.append("\n")
            lines.append(
                f"## [{NEW_VER}] - {date.today().isoformat()}\n"
            )
        else:
            lines.append(line)
    lines.append(f'[{NEW_VER}]: https://github.com/gershnik/keep-awake/releases/v{NEW_VER}\n')

with open(ROOT / "CHANGELOG.md", "wt") as change_log:
    change_log.writelines(lines)




subprocess.run(['git', 'add', ROOT / "CHANGELOG.md", ROOT / "version.json"], check=True)
subprocess.run(['git', 'commit', '-m', f'chore: creating version {NEW_VER}'], check=True)
subprocess.run(['git', 'tag', f'v{NEW_VER}'], check=True)



