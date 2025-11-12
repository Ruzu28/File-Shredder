
# 🗑️ Shredder — Secure File Deletion Utility (Linux, C)

`shredder` is a simple **secure delete** tool written in **C** for Linux systems.
It overwrites files with **random data** multiple times, optionally performs a **final zero pass**, **renames** the file to a random name, and finally **unlinks** it — making recovery by simple disk forensics extremely difficult.

---

## ⚙️ Features

* 🌀 Overwrites file contents with random data for multiple passes
* 🧹 Optional final zero pass (fills file with zeros)
* 🔒 Calls `fdatasync()` and `fsync()` to flush data to disk after each pass
* 🧾 Renames file to a random name before deletion (hides original filename)
* 🪶 Works chunk-by-chunk (no need to load full file into RAM)
* 💬 Verbose mode for detailed progress output
* 📁 Supports multiple files in a single command

---

## 🚀 Compilation

Make sure you’re on a **Linux system** with `gcc` installed.

```bash
gcc -O2 -std=c11 -Wall -Wextra -o shredder shredder.c
```

This will generate an executable named `shredder`.

---

## 🧰 Usage

```
./shredder [-n passes] [-z] [-v] file...
```

### Options:

| Option      | Description                                    |
| ----------- | ---------------------------------------------- |
| `-n passes` | Number of random overwrite passes (default: 3) |
| `-z`        | Perform a final zero pass after random passes  |
| `-v`        | Verbose output (shows progress and status)     |

---

## 🧪 Examples

### 1. Basic 3-pass random overwrite (default)

```bash
./shredder secret.txt
```

### 2. 7 random passes with a final zero pass

```bash
./shredder -n 7 -z secret.txt
```

### 3. Verbose mode (recommended for monitoring)

```bash
./shredder -v important.docx
```

### 4. Multiple files at once

```bash
./shredder -v -n 5 -z file1 file2 file3
```

---

## 🔍 How It Works

1. Opens the target file for writing.
2. Repeatedly overwrites its contents with **cryptographically secure random data** (`getrandom()` or `/dev/urandom`).
3. Optionally performs a final pass with all **zero bytes**.
4. Calls `fdatasync()` and `fsync()` to ensure data is physically written.
5. **Renames** the file to a random hex name in the same directory.
6. **Unlinks** (deletes) the renamed file.
7. Attempts to **sync the directory** to persist rename + deletion.

---

## ⚠️ Security Notes

> ⚠️ **Secure deletion on modern storage is not guaranteed!**

While this tool greatly reduces recoverability on traditional HDDs, it may **not** securely delete files on:

* **SSDs or Flash Drives:** Due to wear-leveling and remapping by firmware.
* **Copy-on-write filesystems:** (e.g., Btrfs, ZFS) may store old copies elsewhere.
* **Journaling or Snapshot-enabled systems:** Data may persist in metadata or snapshots.
* **Network or cloud storage:** Remote or cached copies might remain.

### 🔐 Recommended:

For high-security environments:

* Use **full-disk encryption** and securely destroy encryption keys.
* Use hardware-level **ATA Secure Erase** or manufacturer tools.
* Avoid using this for **SSDs** unless encryption is involved.

---

## 🧩 Example Output (Verbose Mode)

```
Processing secret.txt
Pass 1/3 (random) for secret.txt
Pass 2/3 (random) for secret.txt
Pass 3/3 (random) for secret.txt
Final zero pass for secret.txt
Renamed secret.txt -> /home/user/docs/9d1a7c3e51f2b9f0
Unlinked /home/user/docs/9d1a7c3e51f2b9f0
```

---

## 🧑‍💻 Technical Details

* Language: **C11**
* Tested on: **Linux (x86_64)**
* APIs used:

  * `open()`, `write()`, `lseek()`, `unlink()`, `fsync()`, `fdatasync()`
  * `getrandom()` or `/dev/urandom` for randomness
  * `rename()` and `dirname()` for file renaming
* Buffer size: 1 MiB (adjustable via the `CHUNK` constant)

---

## 🧠 Future Improvements (Optional)

If you wish to extend this project:

* Add **recursive directory shredding**
* Add **secure wipe for free disk space**
* Implement **secure memory handling** (mlock, memset_s)
* Add **progress bar** or percentage indicator
* Create a **GUI** or integrate with a file manager

---

## 🪪 License

This project is provided for **educational and personal use only**.
No warranty is provided for data destruction or recovery prevention.
Use responsibly.



Would you like me to also generate a **man page (Unix manual)** for it (so you can install it under `/usr/local/man/man1/shredder.1`)?
It would look like `man shredder` on Linux.
