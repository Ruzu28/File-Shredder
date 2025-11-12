/*
 * shredder.c
 * Simple secure-delete utility for Linux
 *
 * Compile:
 *   gcc -O2 -std=c11 -Wall -Wextra -o shredder shredder.c
 *
 * Usage:
 *   ./shredder [-n passes] [-z] [-v] file...
 *     -n passes   Number of random overwrite passes (default 3)
 *     -z          Add a final pass of zeros after random passes
 *     -v          Verbose output
 *
 * Limitations: See the program header notes about SSDs, COW filesystems, snapshots, etc.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/random.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <libgen.h>
#include <stdbool.h>

static size_t CHUNK = 1024 * 1024; /* 1 MiB buffer */

static void *alloc_buf(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "malloc(%zu) failed\n", size);
        exit(2);
    }
    return p;
}

static ssize_t fill_random(void *buf, size_t len) {
    /* Try getrandom first */
    ssize_t got = 0;
#if defined(SYS_getrandom) || defined(GRND_NONBLOCK)
    /* use getrandom syscall via wrapper */
    ssize_t r = 0;
    size_t left = len;
    unsigned char *p = buf;
    while (left) {
        r = getrandom(p, left, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            got = -1;
            break;
        }
        left -= r;
        p += r;
    }
    if (left == 0) return (ssize_t)len;
    /* otherwise fall through to /dev/urandom */
#endif

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t r = read(fd, (unsigned char*)buf + total, len - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            total = -1;
            break;
        }
        if (r == 0) break;
        total += r;
    }
    close(fd);
    return total;
}

static int sync_and_check(int fd) {
    /* Try fdatasync then fsync as a fallback */
    if (fdatasync(fd) == 0) return 0;
    if (fsync(fd) == 0) return 0;
    return -1;
}

/* generate random ascii hex filename in same directory */
static char *random_filename_in_dir(const char *orig_path) {
    char *path = strdup(orig_path);
    if (!path) return NULL;
    char *dir = dirname(path); /* NOTE: dirname may modify input */
    size_t name_len = 16; /* 16 hex chars */
    char rndbuf[16];
    if (fill_random(rndbuf, sizeof(rndbuf)) != (ssize_t)sizeof(rndbuf)) {
        free(path);
        return NULL;
    }
    char name[32+1];
    for (int i = 0; i < (int)name_len/2; ++i) {
        sprintf(name + i*2, "%02x", (unsigned char)rndbuf[i]);
    }
    name[name_len] = '\0';
    size_t outlen = strlen(dir) + 1 + strlen(name) + 1;
    char *out = malloc(outlen);
    if (!out) {
        free(path);
        return NULL;
    }
    snprintf(out, outlen, "%s/%s", dir, name);
    free(path);
    return out;
}

static int overwrite_file(const char *path, int passes, bool final_zero, bool verbose) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (verbose) perror("stat");
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        if (verbose) fprintf(stderr, "skipping non-regular file: %s\n", path);
        return -1;
    }

    off_t size = st.st_size;
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        if (verbose) perror("open");
        return -1;
    }

    /* Use a moderate chunk buffer */
    size_t bufsize = (CHUNK < (size_t)size) ? CHUNK : (size_t)size ? (size_t)size : CHUNK;
    void *buf = alloc_buf(bufsize);

    for (int pass = 1; pass <= passes; ++pass) {
        if (verbose) fprintf(stderr, "Pass %d/%d (random) for %s\n", pass, passes, path);
        off_t written_total = 0;
        if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
            if (verbose) perror("lseek");
            free(buf);
            close(fd);
            return -1;
        }
        while (written_total < size) {
            size_t towrite = bufsize;
            if ((off_t)towrite > size - written_total) towrite = (size_t)(size - written_total);
            if (fill_random(buf, towrite) != (ssize_t)towrite) {
                if (verbose) fprintf(stderr, "random generation failed\n");
                free(buf);
                close(fd);
                return -1;
            }
            ssize_t w = write(fd, buf, towrite);
            if (w < 0) {
                if (errno == EINTR) continue;
                if (verbose) perror("write");
                free(buf);
                close(fd);
                return -1;
            }
            written_total += w;
        }
        /* Ensure writes are flushed */
        if (sync_and_check(fd) != 0) {
            if (verbose) perror("sync");
            /* continue anyway, but warn */
        }
    }

    if (final_zero) {
        if (verbose) fprintf(stderr, "Final zero pass for %s\n", path);
        off_t written_total = 0;
        if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
            if (verbose) perror("lseek");
            free(buf);
            close(fd);
            return -1;
        }
        memset(buf, 0, bufsize);
        while (written_total < size) {
            size_t towrite = bufsize;
            if ((off_t)towrite > size - written_total) towrite = (size_t)(size - written_total);
            ssize_t w = write(fd, buf, towrite);
            if (w < 0) {
                if (errno == EINTR) continue;
                if (verbose) perror("write");
                free(buf);
                close(fd);
                return -1;
            }
            written_total += w;
        }
        if (sync_and_check(fd) != 0) {
            if (verbose) perror("sync");
        }
    }

    /* Optionally try to discard physical blocks? Not reliable and may not be desired. */

    free(buf);
    if (close(fd) != 0 && verbose) perror("close");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-n passes] [-z] [-v] file...\n", argv[0]);
        return 1;
    }

    int passes = 3;
    bool final_zero = false;
    bool verbose = false;
    int opt;
    while ((opt = getopt(argc, argv, "n:zv")) != -1) {
        switch (opt) {
            case 'n': passes = atoi(optarg); if (passes < 1) passes = 1; break;
            case 'z': final_zero = true; break;
            case 'v': verbose = true; break;
            default:
                fprintf(stderr, "Usage: %s [-n passes] [-z] [-v] file...\n", argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No files specified\n");
        return 1;
    }

    /* Seed for fallback name changes */
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    int exit_status = 0;
    for (int i = optind; i < argc; ++i) {
        const char *path = argv[i];
        if (verbose) fprintf(stderr, "Processing %s\n", path);

        if (overwrite_file(path, passes, final_zero, verbose) != 0) {
            fprintf(stderr, "Failed to securely overwrite %s\n", path);
            exit_status = 2;
            continue;
        }

        /* rename file to random name to hide original name */
        char *newname = random_filename_in_dir(path);
        if (newname) {
            if (rename(path, newname) != 0) {
                if (verbose) perror("rename");
                free(newname);
            } else {
                if (verbose) fprintf(stderr, "Renamed %s -> %s\n", path, newname);
                /* Optionally try to fsync the directory to persist rename */
                /* Open directory and fsync it */
                char *tmp = strdup(newname);
                char *dir = dirname(tmp);
                int dfd = open(dir, O_DIRECTORY | O_RDONLY);
                if (dfd >= 0) {
                    if (fsync(dfd) != 0 && verbose) perror("fsync(dir)");
                    close(dfd);
                }
                free(tmp);
                /* unlink new name below */
                if (unlink(newname) != 0) {
                    if (verbose) perror("unlink");
                    exit_status = 2;
                } else {
                    if (verbose) fprintf(stderr, "Unlinked %s\n", newname);
                }
                free(newname);
                continue;
            }
        }

        /* If rename failed or not used, unlink original path */
        if (unlink(path) != 0) {
            if (verbose) perror("unlink");
            exit_status = 2;
        } else {
            if (verbose) fprintf(stderr, "Unlinked %s\n", path);
        }
    }

    return exit_status;
}
