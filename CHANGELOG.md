# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

## [2.0.0] - 2025-10-25
### Added
- A major release with many new features
  - keep-awake is now a proper command line tool rather than fire-and-forget Win32 app. 
	It supports command line options and reports output properly. Actual keeping the
	computer awake is done by a child process. Run `keep-awake -h` to see available
	command line options.
  - You can now list currently running keep-awake instances and how much time they have
	remaining. Run `keep-awake list`
  - You can now stop any running keep-awake instances (subject to access checks) using 
	keep-awake tool itself. Run `keep-awake stop pid [pid ...]`
  - Time to keep the computer awake can now be specified using not just seconds but also
	`"<num>d <num>h <num>m <num>[s]"` format where `d` stands for days `h` hours, `m` minutes
	and optional `s` for seconds. Each part is optional but at least one must be present. 
	Don't forget to wrap the whole thing in quotes.
- Releases now provide ARM64 prebuilt binary in addition to x64 one.

### Changed
- Building the code now requires a recent version of Visual Studio 2022 with C++23 support.
- Error messages are no longer reported via Event Log

## [1.0.2] - 2025-04-24
### Changed
- Updated external dependencies
- Build workarounds for recent MSVC bugs

## [1.0.1] - 2023-04-27
### Changed
- Removed dependency on CRT DLLs

## [1.0.0] - 2023-04-27
### Added
- Initial version

[1.0.0]: https://github.com/gershnik/keep-awake/releases/v1.0.0
[1.0.1]: https://github.com/gershnik/keep-awake/releases/v1.0.1
[1.0.2]: https://github.com/gershnik/keep-awake/releases/v1.0.2
[2.0.0]: https://github.com/gershnik/keep-awake/releases/v2.0.0
