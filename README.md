# Keep-Awake

This is a small tool that allows you to keep Windows machine awake. 
This is useful, for example, when connectiong to a Windows machine that can sleep over SSH or other similar
scenarios.

Unlike other solutions to this task this tool doesn't change global computer settings and so doesn't leave
them 'orphaned' if it is abnormally terminated.

## Installation

Just drop `keep-alive.exe` from [Releases](#releases) anywhere on your `%PATH%`.

## Usage

### Keep machine awake until the process is terminated

This mode is usefull when connecting over SSH. All the child processes created within SSH session 
are automatically killed when the session ends. Thus, effectively this mode keeps the computer alive while
the session is active.

```bat
keep-alive
... other commands ...
```

### Keep machine awake for a specified period of time:

If **not** running over SSH:

```bat
keep-alive number-of-minutes
... other commands ...
```

If running over SSH to prevent the process from being killed when SSH session ends you can do this:

* If your shell is CMD
```bat
powershell -Command "Invoke-WmiMethod -Path 'Win32_Process' -Name Create -ArgumentList 'path\to\keep-awake number-of-minutes'"
```

* If your shell is Powershell
```powershell
Invoke-WmiMethod -Path 'Win32_Process' -Name Create -ArgumentList 'path\to\keep-awake number-of-minutes'
```


## Building

Clone this repository and open its folder in Visual Studio as a CMake project.




