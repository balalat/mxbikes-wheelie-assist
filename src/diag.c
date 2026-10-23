#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "diag.h"

void wa_diag(const char *fmt, ...)
{
	char path[MAX_PATH], module[MAX_PATH], *slash, *name;
	HMODULE self = NULL;
	SYSTEMTIME t;
	FILE *f;
	va_list ap;

	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&wa_diag, &self);
	GetModuleFileNameA(self, module, sizeof(module));
	strcpy_s(path, sizeof(path), module);
	slash = strrchr(path, '\\');
	strcpy_s(slash ? slash + 1 : path, sizeof(path) - (slash ? (slash + 1 - path) : 0), "wheelie_assist.log");
	name = strrchr(module, '\\');
	name = name ? name + 1 : module;

	if (fopen_s(&f, path, "a"))
		return;
	GetLocalTime(&t);
	fprintf(f, "%02d:%02d:%02d.%03d [%s] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, name);
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}
