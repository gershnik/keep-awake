# Keep-Awake

This is a small tool that allows you to prevent a Windows machine from sleeping/hibernating. 
This is useful, for example, when connecting over SSH to a Windows machine that is configured to sleep when not used. 

Unlike other solutions to this task `keep-awake` **doesn't change global computer settings** and so doesn't leave
them 'orphaned' if it is abnormally terminated.

## Installation

Just drop `keep-awake.exe` for your architecture from [Releases](https://github.com/gershnik/keep-awake/releases) 
anywhere on your `%PATH%`.

## Usage

`keep-awake` is a command-line application that works by launching a background copy of itself 
which prevents the computer from sleeping.
This allows you to continue your work while it is running in background.

You can see the available command line options by running `keep-awake --help`

### Keep machine awake until the process is terminated

This mode is usefull when connecting over SSH. Windows SSH server kills all the child processes created within SSH session 
when the session ends. Thus, effectively, this mode keeps the computer alive while the session is active.

```bat
keep-awake
... other commands ...
```

### Keep machine awake for a specified period of time:

You can pass an optional timeout argument to `keep-awake` (see [below](#timeout-syntax) for syntax). 

If **not** running over SSH:

```bat
keep-awake <timeout>
... other commands ...
```

When running over SSH you can prevent `keep-alive` process from being killed when SSH session ends using:

* If your shell is CMD
```bat
powershell -Command "Invoke-WmiMethod -Path 'Win32_Process' -Name Create -ArgumentList 'path\to\keep-awake <timeout>'"
```

* If your shell is Powershell
```powershell
Invoke-WmiMethod -Path 'Win32_Process' -Name Create -ArgumentList 'path\to\keep-awake <timeout>'
```

#### Timeout syntax

The syntax for `<timeout>` can be a single number - this is intepreted as seconds. 
Or, you can use a full format:
```
"<num>d <num>h <num>m <num>[s]"
```
For days, hours, minutes and seconds. Every part is optional, but at least one must be present.

You can use any number of spaces (including none) anywhere in the string but, if you do, yo will need to 
wrap the string in `"` to make it one command line argument.


### Listing currently active instances

You can list currently active background instances of `keep-awake` and how long they have left 
to run.

```
keep-awake list
```

### Stopping an intance

You can stop running instances of `keep-awake` via:

```
keep-awake stop pid [pid ...]
```

where pid is a process ID of a running instance. The process IDs are reported when you launch `keep-awake`
or by `list` command.

Alternatively, you can always terminate an instance using Task Manager or a similar tool.

## Building

Clone this repository and open its folder in Visual Studio 2022 or later as a CMake project.



