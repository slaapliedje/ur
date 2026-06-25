# Patches

Local patches to third-party projects (not vendored here — applied to your own clone).

## `fujinet-pc-gcc16-glibc.patch`

Lets **FujiNet-PC** build with a bleeding-edge toolchain (GCC 16 / current glibc),
which is needed to run an emulated FujiNet for online testing (see
[`tools/online-test.sh`](../online-test.sh)). Two source fixes:

- `lib/utils/string_utils.cpp` — comparator parameters `char&` → `const char&`
  (current libstdc++ `std::search`/`std::equal` pass `const`).
- `lib/compat/linux_termios2.h` — modern glibc defines `BOTHER`, which skipped the
  shim block that *also* defined `LINUX_IBSHIFT` / `struct termios2`; decoupled them
  so each is defined independently. (That path is for physical serial, not NetSIO.)

Apply to a fresh clone and build the Atari target:

```sh
git clone https://github.com/FujiNetWIFI/fujinet-pc.git
cd fujinet-pc
git apply /path/to/ur/tools/patches/fujinet-pc-gcc16-glibc.patch
cmake -DTARGET=ATARI -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -S . -B build
cmake --build build -j"$(nproc)"
# -> build/fujinet
```

Only needed on very new toolchains; older GCC builds upstream unmodified. The
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` flag works around a bundled component's
ancient `cmake_minimum_required` under CMake 4.x.
