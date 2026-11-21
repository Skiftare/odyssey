#pragma once

/* https://github.com/postgres/postgres/blob/master/src/include/port/win32_port.h
 * windows.h includes a lot of other headers, slowing down compilation
 * significantly.  WIN32_LEAN_AND_MEAN reduces that a bit. It'd be better to
 * remove the include of windows.h (as well as indirect inclusions of it) from
 * such a central place, but until then...
 *
 * To be able to include ntstatus.h tell windows.h to not declare NTSTATUS by
 * temporarily defining UMDF_USING_NTSTATUS, otherwise we'll get warning about
 * macro redefinitions, as windows.h also defines NTSTATUS (yuck). That in
 * turn requires including ntstatus.h, winternl.h to get common symbols.
 */
