#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

int search_keyword(const char *buffer, size_t buffer_size, const char *keyword) {
    for (size_t i = 0; i <= buffer_size - strlen(keyword); i++) {
        if (memcmp(buffer + i, keyword, strlen(keyword)) == 0) {
            return 1;  
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <pid> <search_string>\n", argv[0]);
        return 1;
    }

    int pid = atoi(argv[1]);
    const char *search_string = argv[2];

    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE *maps_file = fopen(maps_path, "r");
    if (!maps_file) {
        perror("Failed to open /proc/[pid]/maps");
        return 1;
    }

    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    int mem_fd = open(mem_path, O_RDONLY);
    if (mem_fd == -1) {
        perror("Failed to open /proc/[pid]/mem");
        fclose(maps_file);
        return 1;
    }

    unsigned long start, end;
    char perms[5];
    char line[256];

    while (fgets(line, sizeof(line), maps_file)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
            if (perms[0] != 'r') continue;  // skip non-readable regions

            size_t region_size = end - start;
            char *buffer = malloc(region_size);
            if (!buffer) {
                fprintf(stderr, "Memory allocation failed\n");
                break;
            }

            ssize_t bytes_read = pread(mem_fd, buffer, region_size, start);
            if (bytes_read > 0) {
                if (search_keyword(buffer, bytes_read, search_string)) {
                    printf("Found keyword in memory region: %lx-%lx\n", start, end);
                }
            } else {
                if (errno != EIO) {
                    perror("Error reading /proc/[pid]/mem");
                    free(buffer);
                    break;
                }
            }

            free(buffer);
        }
    }

    fclose(maps_file);
    close(mem_fd);
    return 0;
}
