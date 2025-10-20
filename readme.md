# ARMv8l Neon Accelerated Diff

This is a high performance diff utility tailored for **ARMv8 32bit Linux systems**, leveraging **ARMv8 hardware CRC32 instructions** and **Neon SIMD** to speed up text comparison dramatically compared to traditional diff tools.

---

## Overview

This tool computes differences between two text files and outputs results in a universally recognized GNU unified diff format, ensuring compatibility with standard `*nix` workflows.

Unlike traditional line by line string comparisons, it uses hardware accelerated hashing combined with vectorized instructions to achieve speed and efficiency.

---

## Core Components and Algorithms

### 1. Input Preprocessing
- **Trimming whitespace:** Each line is stripped of leading and trailing whitespace to avoid detecting meaningless differences.
- **Normalization:** Line endings (especially CRLF vs LF) are normalized to ensure consistent hashing.

### 2. Symbolic Line Hashing
- Uses the **ARMv8 CRC32 hardware instruction** to generate a 32bit hash for each input line. This is a fast, hardware accelerated checksum that balances speed with low collision probability.
- The hashing processes the line 4 bytes at a time using the `__crc32w` intrinsic, then completes byte wise hashing for remaining bytes.
- These hashes serve as symbolic fingerprints for quick equality checks.

### 3. Memory Management
- Lines and their hashes are stored in dynamically resizable arrays.
- Allocations are **cache line aligned (64 bytes)** for optimal CPU cache usage, minimizing latency on memory accesses.

### 4. SIMD Accelerated Batch Comparisons
- Line hashes are compared in batches using **Neon SIMD intrinsics**.
- The code loads four 32bit hashes simultaneously into Neon 128bit registers and compares in parallel using `vceqq_u32`.
- Results are translated into bitmasks indicating which lines match or differ, reducing branching and speeding up diff operations.

### 5. Collision Verification
- Because hashing can produce collisions, suspected equal hash pairs are verified with a **Neon bytewise vector comparison**, checking 16 bytes at a time.
- A final fallback byte by byte check ensures correctness.

### 6. Diff Algorithm
- Implements a Myers like heuristic diff adapted to operate on line hashes rather than raw strings for speed.
- This algorithm traverses the sequences of hashes and detects:
  - **Unchanged lines** (matching hashes and verified contents),
  - **Inserted lines** in the second file,
  - **Deleted lines** from the first file.
- The heuristic looks ahead to improve detection of shifted lines.

### 7. Unified Diff Output
- Produces output in classic GNU unified diff format:
  - Headers with filenames,
  - Hunks of changes with context lines,
  - Line numbers and line type prefixes (` `, `+`, `-`).

This makes results immediately usable with `patch` and other `*nix` tools.

---

## ARMv8 Neon and CRC32 Details

- **Neon SIMD:** ARMs Advanced SIMD extension provides 32 registers of 128 bits each, capable of performing parallel operations on multiple data elements simultaneously.
- **CRC32 Extensions:** ARMv8 includes hardware crypto extensions facilitating fast CRC32 computation with intrinsic support, drastically accelerating hashing tasks.
- The code utilizes Neon intrinsics like `vld1q_u32`, `vceqq_u32`, and bytewise comparisons using `vceqq_u8` for vectorized equality checks, minimizing CPU cycles and branch mispredictions.

---

## Performance

Benchmarks on ARMv8 32bit show that this diff implementation typically runs faster than GNU diff for typical source files, due to:
- Hardware accelerated CRC32 hashing.
- Vectorized batch hashing and comparisons with Neon.
- Reduced expensive bytewise comparisons through careful hashing and SIMD verification.

This makes it a compelling choice for ARM based systems, particularly embedded or resource constrained devices where performance is critical.

---

## Getting Started

1. Compile with ARMv8 Neon and CRC32 flags:
```
gcc -march=armv8-a+simd+crc -O3 -o diff diff.c
```

2. Or just use make:
```
make
```

---

## Potential Improvements & Benchmarking Scripts

1. Potential improvements include adding multi threading to leverage multiple CPU cores for even faster diffing, optimizing memory usage and caching strategies for very large files, and refining the hashing collision detection for edge cases.

2. Developing POSIX shell scripts for automated testing and benchmarking can help regularly compare performance and correctness against the system GNU diff tool on diverse datasets. These scripts can generate large controlled random files, run both diff implementations, capture timing data, and summarize differences to ensure ongoing optimization and reliability.

---

## License & Contribution

This project is licensed under the GNU General Public License v3 (GPL-3.0). It ensures that any modifications or derivative works remain open source under the same license, promoting collaborative improvement and freedom to use, modify, and share the code.

This code is experimental, focused on exploring ARM Neon hardware acceleration in diff algorithms. Contributions that improve performance, portability, or functionality are highly welcome. Please submit pull requests or issues to help enhance this project.

---
