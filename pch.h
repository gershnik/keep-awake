#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.
// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.

#include <WinSDKVer.h>
#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>

#define ARGUM_USE_EXPECTED

#include <argum/parser.h>
#include <argum/type-parsers.h>

#include <format>
#include <io.h>
