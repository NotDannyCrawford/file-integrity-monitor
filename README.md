# file-integrity-monitor

A small command-line tool in C++ that detects when files have been **added, modified, or deleted** in a directory. It fingerprints every file, stores a baseline in SQLite, and compares each new scan against that baseline — the same core idea behind the file integrity monitoring (FIM) tools used for tamper and ransomware detection.

## What it does

On each run it walks a directory, hashes every file, and compares the result to a stored baseline:

| Status | Meaning |
|---|---|
| `[NEW]` | a file that wasn't in the baseline |
| `[MODIFIED]` | a file whose contents changed (its hash differs) |
| `[DELETED]` | a file that was in the baseline but is now gone |
| `[OK]` | unchanged since last scan |

The baseline lives in a local SQLite database, so the tool "remembers" state between runs.

## Example

```text
$ ./fim testfiles
Scanning: testfiles
  [NEW]      notes.txt
  [NEW]      data.txt

# ...later, after editing notes.txt and removing an old file:
$ ./fim testfiles
Scanning: testfiles
  [MODIFIED] notes.txt
  [OK]       data.txt
  [DELETED]  temp.txt
```

## How it works

1. **Hash** — each file's bytes are run through an FNV-1a hash to produce a compact fingerprint. Any change to the file changes the fingerprint.
2. **Store** — filename, hash, size, and a timestamp are saved to a SQLite table (`files`).
3. **Compare** — the next scan looks up each file's stored hash and reports new / modified / deleted, then updates the baseline.

All database writes use **prepared statements with bound parameters**, so a filename can never be interpreted as SQL (protection against SQL injection).

## Build & run

Requires a C++17 compiler and SQLite (ships with macOS and most Linux distros).

```bash
g++ -std=c++17 main.cpp -o fim -lsqlite3
./fim <folder_to_watch>
```

## Design notes

- **FNV-1a** keeps the tool dependency-free and easy to read. A security-hardened version would use a cryptographic hash like **SHA-256** so an attacker couldn't deliberately forge a file with a matching fingerprint.
- The baseline is stored in `baseline.db`, which is git-ignored since it's generated data.

## Possible next steps

- **Entropy check** to flag files that suddenly look encrypted — an early ransomware signal.
- Swap FNV-1a for **SHA-256**.
- Recursive directory scanning and an end-of-run summary.

---

Author: Daniel Crawford
