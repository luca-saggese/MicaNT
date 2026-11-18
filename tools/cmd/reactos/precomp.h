#ifndef __CMD_PRECOMP_H
#define __CMD_PRECOMP_H

/*
 * Luce adaptation (2026-11-18): this translation unit no longer consumes
 * Wine's debug headers or debug channel.  Keep the upstream GPL notices in
 * the source files; Luce diagnostics are provided by the native runtime.
 */

#ifdef _MSC_VER
#pragma warning ( disable : 4103 ) /* use #pragma pack to change alignment */
#undef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE
#endif /*_MSC_VER */

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <tchar.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winreg.h>
#include <winuser.h>
#include <wincon.h>
#include <winioctl.h>
#include <winternl.h>
#include <shellapi.h>

#include <strsafe.h>

#include "../cmd_cpp_compat.h"

#include <conutils.h>

#include "resource.h"

#include "cmd.h"
#include "config.h"
#include "batch.h"

#ifndef ASSERT
#define ASSERT(expression) ((void)0)
#endif

/* ReactOS CMD's optional Wine tracing is deliberately not part of the Luce
 * build.  Command behavior and user-visible errors remain in the CMD code;
 * these macros only remove compile-time logging hooks. */
#define TRACE(...) ((void)0)
#define WARN(...)  ((void)0)
#define ERR(...)   ((void)0)
#define FIXME(...) ((void)0)
#define debugstr_aw(value) (value)

#endif /* __CMD_PRECOMP_H */
