#include "deque.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

bool volatile sigint = false;

// whether user wants to filter by or not
typedef struct criteria {
    bool filename;
    bool size;
    bool type;
    bool permissions;
    bool noLinks;
} criteria;

typedef struct fileInfo {
    char *filename;
    int size;
    char type;
    char *permissions;
    int noLinks;
} fileInfo;

enum return_status {
    SUCCESS,
    NO_FILE_FOUND,
    S_SIGINT,
    ERROR
};

void usage(char *argv0);

int matchString(const char *str, char *strRegex);

static void sigHandler(int sig);

int print(char *name, fileInfo filter, criteria filterBy, int indentation, struct deque *dirQueue);

int search(fileInfo filter, criteria filterBy, char *path);

char *escapeChar(const char *str, char c);

bool match(fileInfo file, fileInfo filter, criteria filterBy);

char *concatFiles(const char *path, const char *file);

void indent(int count);

char getFileType(char d_type);

void getPermissions(char *permissions, mode_t mode);

int main(int argc, char **argv) {
    criteria filterBy = {false, false, false, false, false};
    fileInfo f;
    bool pathInput;
    char *path;

    signal(SIGINT, sigHandler);

    int opt;

    while ((opt = getopt(argc, argv, "f:b:t:p:l:w:")) != -1) {
        switch (opt) {
            case 'f':
                filterBy.filename = true;
                f.filename = optarg;
                break;
            case 'b':
                filterBy.size = true;
                f.size = atoi(optarg);
                break;
            case 't':
                if (strlen(optarg) != 1) {
                    fprintf(stderr, "Invalid fileType\n");
                    usage(argv[0]);
                }
                filterBy.type = true;
                f.type = optarg[0];
                break;
            case 'p':
                filterBy.permissions = true;
                f.permissions = optarg;
                if (strlen(f.permissions) != 9) {
                    fprintf(stderr, "Invalid permissions\n");
                    usage(argv[0]);
                }
                break;
            case 'l':
                filterBy.noLinks = true;
                f.noLinks = atoi(optarg);
                break;
            case 'w':
                pathInput = true;
                path = optarg;
                break;
            default:
                usage(argv[0]);
        }
    }

    if (!pathInput) {
        fprintf(stderr, "-w option is mandatory\n");
        usage(argv[0]);
        return 0;
    }

    if (!(filterBy.filename || filterBy.size || filterBy.type ||
          filterBy.permissions || filterBy.noLinks)) {
        fprintf(stderr, "at least one criteria needs to be specified\n");
        usage(argv[0]);
        return 0;
    }

    search(f, filterBy, path);

    return 0;
}

// returns permissons string given mode_t mode
void getPermissions(char *permissions, mode_t mode) {
    permissions[0] = (mode & S_IRUSR) ? 'r' : '-';
    permissions[1] = (mode & S_IWUSR) ? 'w' : '-';
    permissions[2] = (mode & S_IXUSR) ? 'x' : '-';
    permissions[3] = (mode & S_IRGRP) ? 'r' : '-';
    permissions[4] = (mode & S_IWGRP) ? 'w' : '-';
    permissions[5] = (mode & S_IXGRP) ? 'x' : '-';
    permissions[6] = (mode & S_IROTH) ? 'r' : '-';
    permissions[7] = (mode & S_IWOTH) ? 'w' : '-';
    permissions[8] = (mode & S_IXOTH) ? 'x' : '-';
    permissions[9] = '\0';
}

char getFileType(char d_type) {
    switch (d_type) {
        case DT_BLK:
            return 'b';
        case DT_CHR:
            return 'c';
        case DT_DIR:
            return 'd';
        case DT_REG:
            return 'f';
        case DT_FIFO:
            return 'f';
        case DT_LNK:
            return 'l';
        default:
            fprintf(stderr, "%s\n", "File type is not supported");
            return '?';
    }
}

void indent(int count) {
    if (count >= 0)
        printf("|--");
    for (int i = 0; i < count; ++i)
        printf("--");
}

char *concatFiles(const char *path, const char *file) {
    char *f = (char *) malloc(strlen(path) + strlen(file) + 1 + sizeof('/'));
    strcpy(f, path);
    strcat(f, "/");
    strcat(f, file);
    return f;
}

int search(fileInfo filter, criteria filterBy, char *path) {
    struct deque d;
    initDeque(&d);
    addFront(&d, path); // to be able to print target directory
    enum return_status res = print(path, filter, filterBy, 0, &d);
    switch (res) {
        case NO_FILE_FOUND:
            printf("No file found\n");
            break;
        case S_SIGINT:
            fprintf(stderr, "Program interrupted by signal: SIGINT\n");
            break;
        case ERROR:
				default:
            fprintf(stderr, "Unknown error\n");
    }
    freeDeque(&d);
    return 0;
}

bool match(fileInfo file, fileInfo filter, criteria filterBy) {
    if (filterBy.size && file.size != filter.size)
        return false;
    if (filterBy.type && file.type != filter.type)
        return false;
    if (filterBy.noLinks && file.noLinks != filter.noLinks)
        return false;
    if (filterBy.permissions && strcmp(file.permissions, filter.permissions) != 0)
        return false;
    if (filterBy.filename && matchString(file.filename, filter.filename) != 0)
        return false;
    return true;
}

int print(char *name, fileInfo filter, criteria filterBy, int indentation, struct deque *dirQueue) {
    struct dirent *pDirent; // do not attempt to free (stated in readdir man)
    DIR *pDir;
    char *filePath;
    char permissions[9];
    bool found = false;

    pDir = opendir(name);
    if (pDir == NULL) {
        perror("Cannot open directory");
        fprintf(stderr, "Cannot open directory: '%s'\n", name);
        return ERROR;
    }

    while (!sigint && (pDirent = readdir(pDir)) != NULL) {
        fileInfo f;
        // get stats
        f.filename = pDirent->d_name;
        f.type = getFileType(pDirent->d_type);
        struct stat stats;
        // ignore . and ..
        if (!strcmp(f.filename, ".") || !strcmp(f.filename, ".."))
            continue;
        filePath = concatFiles(name, f.filename);
        // in case of error on getting stats, continue with next file
        if (f.type == 'l') {
            if (lstat(filePath, &stats) != 0) {
                perror(filePath);
                free(filePath);
                continue;
            }
        } else if (stat(filePath, &stats) != 0) {
            perror(filePath);
            free(filePath);
            continue;
        }
        getPermissions(permissions, stats.st_mode);
        f.permissions = permissions;
        f.size = stats.st_size;
        f.noLinks = stats.st_nlink;

        bool matched = false;
        if (match(f, filter, filterBy)) {
            found = matched = true;
            // path should be printed relative to found file
            int dirIndent = indentation - dirQueue->size;
            // print parent directories
            while (dirQueue->size > 0) {
                char *file = delRear(dirQueue);
                indent(dirIndent++);
                printf("%s\n", file);
            }
            // print file
            indent(indentation);
            printf("%s\n", f.filename);
        }
        if (f.type == 'd') { // recursive search
            if (!matched)
                addFront(dirQueue, f.filename); // to keep parent directories
            bool foundInSub = print(filePath, filter, filterBy, indentation + 1, dirQueue) == 0;
            found = found || foundInSub;
            delFront(dirQueue); // if not found in the sub-directories, remove parents
        }
        free(filePath);
    }
    closedir(pDir);
    if (sigint)
        return S_SIGINT;
    else if (!found)
        return NO_FILE_FOUND;
    return SUCCESS;
}

void usage(char *argv0) {
    printf(
            "Usage: %s -w targetDirectoryPath [-f filename] [-b file size] [-t file "
            "type] "
            "[-p permissions] [-l number of links] \n\n"
            " -f filename       Filename, case insensitive, supporting the following "
            "reqular expressin: +\n"
            " -b file size      File size in bytes\n"
            " -t file type      File type (d: directory, s: socket, b: block device, "
            "c: character device f: regular file, p: pipe, l: symbolic link)\n"
            " -p rwxr-xr--      Permissions, as 9 characters\n"
            " -l number of links\n"
            " -w path           The path in which to search recursively (i.e. across "
            "all of its subtrees)\n\n"
            "At least one of the search criteria must be employed.\n",
            argv0);
    exit(EXIT_FAILURE);
}

char *escapeChar(const char *str, char c) {
    int length = strlen(str) + 1;
    char *out = (char *) malloc(length * sizeof(char));
    if (out == NULL) {
        perror("Malloc error");
        exit(1);
    }
    int si = 0;
    int oi = 0;
    while (str[si]) {
        if (str[si] == c) {
            out = realloc(out, ++length * sizeof(char));
            out[oi] = '\\';
            ++oi;
            out[oi] = c;
        } else {
            out[oi] = str[si];
        }
        ++oi;
        ++si;
    }
    out[oi] = '\\';
    return out;
}

// returns 0 if given str matches with  strRegex, -1 otherwise
int matchString(const char *str, char *strRegex) {
    char *s = strdup(str);

    // case insensitive
    for (int i = 0; s[i]; ++i)
        s[i] = tolower(s[i]);
    for (int i = 0; strRegex[i]; ++i)
        strRegex[i] = tolower(strRegex[i]);

    char *escaped = escapeChar(str, '+');

    if (strcmp(escaped, strRegex) == 0) {
        free(escaped);
        free(s);
        return 0;
    }
    free(escaped);

    int si, sr;
    si = sr = 0;
    while (strRegex[sr]) {
        if (strRegex[sr] == '+' && sr != 0) {
            char rep = strRegex[sr - 1]; // repeated character
            int consumed = 0;
            sr++;
            while (rep == s[si]) {
                si++; // consume repeated character
                consumed++;
            }
            // in case of repeated character also has occurance right after +
            // need consume regex char while counting of number of consumed char.
            // so we can find out minimum required repetation
            while (rep == strRegex[sr]) {
                sr++;
                consumed--;
            }
            if (consumed < 0) {
                free(s);
                return -1;
            }

            if (!strRegex[sr]) { // in case of + character is at the end
                free(s);
                return 0;
            }
        }
        if (s[si] != strRegex[sr]) {
            free(s);
            return -1;
        }
        si++;
        sr++;
    }
    free(s);
    return 0;
}

static void sigHandler(int sig) {
    sigint = true;
}
