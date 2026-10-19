# Verification

## Checked on 19 October 2026

The actual C core was compiled and run on Windows x64 with Zig 0.17.0 using its Clang 22.1.8 C frontend. The build enabled `-std=c99 -Wall -Wextra -Wpedantic -Werror`. There were no compiler warnings.

The CMake Debug and Release builds each passed 19 cases and 6307 checks. Each build also passed both CTest entries, including the host demonstration. `sizeof(gw_gateway)` was 728 bytes on this host, including both 256 byte queues. This is not an ARM memory measurement.

The Python build helper was also exercised successfully. The CMake 3.22.1 / Ninja runs used an external compiler wrapper and explicit LLVM archive tools; those machine specific files are not repository content. The snapshot test copies object bytes explicitly so its comparison does not depend on whether structure assignment preserves padding. The forwarding core itself was unchanged during publication preparation.

The installed MSVC toolchain could not complete a build because its C runtime SDK headers were missing. The MSVC path in the build helper remains unverified on a complete installation. macOS and sanitizer runs have not been performed. Linux results are recorded below.

## Continuous integration

The [workflow](../.github/workflows/ci.yml) defines GCC and Clang jobs on Ubuntu 24.04. Each job configures a Debug build with CMake, builds both executables, and runs CTest with missing tests treated as an error. It runs on pushes and pull requests to `main`, or by manual dispatch.

The workflow passed local validation with actionlint 1.7.12. Its checkout action is pinned to a reviewed commit, uses only read access, and does not persist credentials. No private deployment secrets are required.

On 20 October 2026, the first [GitHub Actions run](https://github.com/darkimorta5/stm32-ssh-uart-gateway/actions/runs/39332333311) passed for commit `eb013f84458a06db17af7f9bfd6ab657a2c1d645`. Both GCC and Clang jobs built the project and passed both CTest entries on Ubuntu. These are host results, not STM32 hardware validation.

## What the C tests cover

* Passive startup and idle behavior, including an opaque synthetic boot transcript.
* Exclusive ownership, BUSY isolation, stale tokens, and token exhaustion.
* Unchanged control and binary bytes in both directions.
* Partial writes, repeated queue wrap, blocked callbacks, and zero progress.
* Explicit input overflow, receive overflow, and discarded byte accounting.
* Close, repeated sessions, TCP abort, USB detach, and simulated reconnect.
* Reported line errors, USB errors, partial acceptance followed by failure, and invalid callback reports.
* SSH EOF distinct from Ctrl+D, continued output, deadline handling across a timer wrap, and read only snapshots.

These tests call the public core with synthetic transport callbacks. They do not test an SSH library, actual TCP packets, FTDI hardware, USB enumeration, a terminal client, or an attached operating system.

## Before claiming a hardware implementation

A board integration still needs an ARM build and memory map review, authentication and cryptography review, USB compatibility tests, and real terminal sessions. Check close and reconnect, two clients, control characters, slow output, overflow, cable removal, and independent restarts of the board and target.

Measure autonomous UART TX during boot separately from user input. Report transport acceptance, cancellation, deadlines, and discarded bytes separately. A visible prompt is not a byte integrity test.

No hardware result from a private or earlier implementation is attributed to this repository.
