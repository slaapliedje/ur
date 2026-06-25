; SPDX-License-Identifier: GPL-3.0-or-later
;
; Compatibility shim: alias the C-stack zeropage pointer c_sp -> sp.
;
; fujinet-lib's prebuilt libraries are built with current cc65, which renamed the
; C parameter-stack zeropage symbol `sp` to `c_sp`. An older cc65 (<= 2.19) runtime
; still exports `sp`, so linking the network code fails with "Unresolved external
; 'c_sp'". This re-exports `c_sp` at the same address as the runtime's `sp`.
;
; Only needed / safe with OLD cc65. Enable it via `CSP_COMPAT=1 make atari`.
; With a current cc65 (which defines c_sp itself) leave it OFF to avoid a clash.

        .importzp sp
        .exportzp c_sp

c_sp = sp
