// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#if defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <winsock2.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <iostream>
#include <xstring>
#include <deque>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <assert.h>
#include <thread>
