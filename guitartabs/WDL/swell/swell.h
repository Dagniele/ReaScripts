// Minimal types so sdk/reaper_plugin.h can be compiled without the full
// Cockos WDL/SWELL tree. This is not the SWELL implementation.
#ifndef _WDL_SWELL_H_
#define _WDL_SWELL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef intptr_t INT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t UINT_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef int INT;
typedef int LONG;
typedef unsigned int ULONG;
typedef short SHORT;
typedef signed char BOOL;

typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;

typedef void* HANDLE;
typedef void* HINSTANCE;
typedef void* HGLOBAL;

struct HWND__ {
  int unused;
};
struct HMENU__ {
  int unused;
};
typedef struct HWND__* HWND;
typedef struct HMENU__* HMENU;

typedef struct {
  LONG x;
  LONG y;
} POINT;

typedef struct {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT;

typedef struct {
  unsigned char fVirt;
  unsigned short key;
  unsigned short cmd;
} ACCEL;

typedef struct _GUID {
  unsigned int Data1;
  unsigned short Data2;
  unsigned short Data3;
  unsigned char Data4[8];
} GUID;

typedef struct {
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
} MSG;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#endif
