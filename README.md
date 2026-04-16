# Schrödinger's Database (SDB)

> *"In SDB, your data is safe as long as you don't care to see it."*

A non-deterministic data storage system where the act of querying data is the primary cause of its destruction.

## Overview

SDB operates on the **Observer Effect**: every data retrieval has a 50% chance of permanently erasing the record instead of returning it. The database exists in a state of superposition until a query is executed, at which point the wave function collapses.

## Commands

| Command | Equivalent | Success Rate | Description |
|---------|-----------|-------------|-------------|
| `PRAY`    | INSERT  | 100%  | Commit data to the void. |
| `OBSERVE` | SELECT  | 50%   | Retrieve data. 50% risk of permanent erasure. |
| `MUTATE`  | UPDATE  | ~25%  | Modify data. Two sequential entropy gates (50% × 50%). |
| `FORGET`  | DELETE  | 100%  | The only command that functions with absolute certainty. |
| `COUNT`   | COUNT(*) | —    | Destructive count. Each record observed individually. |

## Building

```bash
# Release build
make

# Debug build (AddressSanitizer + UndefinedBehaviorSanitizer)
make debug

# Run tests
make test

# Clean
make clean
```

**Requirements:** GCC with C11 support, Linux (uses `/dev/random`, `/dev/urandom`, and POSIX APIs).

## Usage

```bash
# Store data
$ ./sdb pray "important information"
a3f1c2e4d5b6789012345678abcdef01

# Attempt retrieval (50% chance of survival)
$ ./sdb observe a3f1c2e4d5b6789012345678abcdef01
important information

# Or...
$ ./sdb observe a3f1c2e4d5b6789012345678abcdef01
Status 410: Record underwent permanent wave function collapse during retrieval.

# Attempt mutation (~25% success rate)
$ ./sdb mutate a3f1c2e4d5b6789012345678abcdef01 "updated data"

# Delete with certainty
$ ./sdb forget a3f1c2e4d5b6789012345678abcdef01

# Destructive count (each record has 50% collapse risk)
$ ./sdb count
Surviving: 47
Collapsed: 53
```

### Options

```
--data-dir <path>         Storage directory (default: ./sdb_data)
--deterministic <seed>    Use deterministic entropy source (for testing)
--help                    Show help message
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0    | Success |
| 1    | Record collapsed during operation |
| 2    | Record not found |
| 3    | System error |

## Architecture

- **Volatility Engine**: File-backed storage with a Quantum-Decoupled Index (QDI). Never reads record content during index operations (Non-Verification Protocol).
- **Entropy Trigger**: Reads from `/dev/random` for true hardware randomness. Deterministic mode available for testing.
- **Secure Erase**: Zero-fill arena region + `msync` + return to freelist.
- **Arena Storage**: Single mmap'd arena (`sdb_arena.dat`) with in-band freelist.

## License

This project is provided as-is. Use at your own risk — quite literally.
