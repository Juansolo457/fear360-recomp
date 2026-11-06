// Imports the public tools' prebuilt libraries (built with VS .NET 2003) expect from msvcr71.dll
// that the Universal CRT provides only as inline functions. Each one becomes a pointer named
// like the import slot the old objects call through.
#include <stdarg.h>
#include <stdio.h>

static int __cdecl shim_snprintf(char* buffer, size_t count, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int written = _vsnprintf(buffer, count, format, args);
	va_end(args);
	return written;
}

// x86 C names get one leading underscore, so this is the import slot __imp___snprintf.
int (__cdecl* _imp___snprintf)(char*, size_t, const char*, ...) = shim_snprintf;
