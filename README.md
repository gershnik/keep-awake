# Keep-Awake

This is a small tool that allows you prevent a Windows machine from sleeping/hybernating. 
This is useful, for example, when connectiong to a Windows machine that is configured to sleep when not used 
over SSH. 

Unlike other solutions to this task `keep-awake` doesn't change global computer settings and so doesn't leave
them 'orphaned' if it is abnormally terminated.

## Installation

Just drop `keep-awake.exe` from [Releases](https://github.com/gershnik/keep-awake/releases) anywhere on your `%PATH%`.

## Usage

### Keep machine awake until the process is terminated

This mode is usefull when connecting over SSH. All the child processes created within SSH session 
are automatically killed when the session ends. Thus, effectively this mode keeps the computer alive while
the session is active.

```bat
keep-awake
... other commands ...
```

### Keep machine awake for a specified period of time:

If **not** running over SSH:

```bat
keep-awake seconds-to-keep-awake
... other commands ...
```

If running over SSH to prevent the process from being killed when SSH session ends you can do this:

* If your shell is CMD
```bat
powershell -Command "Invoke-WmiMethod -Path 'Win32_Process' -Name Create -ArgumentList 'path\to\keep-awake seconds-to-keep-awake'"
```

* If your shell is Powershell
```powershell
Invoke-WmiMethod -Path 'Win32_Process' -Name Create -ArgumentList 'path\to\keep-awake seconds-to-keep-awake'
```


## Building

Clone this repository and open its folder in Visual Studio as a CMake project.




