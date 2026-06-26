; SPDX-License-Identifier: GPL-3.0-or-later
;
; Compatibility shims for linking fujinet-lib (built with current cc65) against an
; OLD cc65 (<= 2.19) runtime. Current cc65 renamed some internal symbols; this
; re-exports the new names at the old runtime's equivalents:
;
;   c_sp     <- sp        the C parameter-stack zeropage pointer (renamed sp->c_sp).
;                         Without it the network code fails: "Unresolved external 'c_sp'".
;   ___bzero <- __bzero   the appkey code calls the internal bzero (C `__bzero`); the
;                         2.19 runtime exports it as C `_bzero` (asm `__bzero`). Same
;                         (ptr, n) contract, so aliasing is safe.
;   ___oserror <- __oserror  the C64 IEC open (fuji_cbm_open.s) reads the OS-error
;                         variable as C `__oserror`; the 2.18 runtime exports it as
;                         C `_oserror` (asm `__oserror`). Same byte, so alias it.
;
; Only needed / safe with OLD cc65; the Makefile auto-enables it (CSP_COMPAT) when
; the runtime lacks c_sp. With a current cc65 leave it OFF to avoid a clash.

        .importzp sp
        .exportzp c_sp

c_sp = sp

        .import __bzero
        .export ___bzero

___bzero = __bzero

        .import __oserror
        .export ___oserror

___oserror = __oserror
