/*
    The test info block is for mp_tester.php to check if the result is good.
    the mark below is for tester to find the json below.
    TEST_INFO_BLOCK
    {
        "marks": [
            "all nb lock done, count",
            "thread pool still usable"
        ],
        "timeout": 10
    }
*/

// this test may be failed on mcaOS, __close_nocancel on macOS is buggy, and will stuck forever

// manully build
// cc -g -I../../include -I../../deps/libuv/include -DHAVE_LIBCAT flock_unix.c ../../build/libcat.a -lpthread -lssl -lcrypto -lcurl -ldl -o flock_unix
// manully run
// ./flock_unix

#include "cat.h"
#include "cat_api.h"
#include "cat_fs.h"
#include <errno.h>
#include <string.h>

int child(int rfd, int wfd);
int parent(int rfd, int wfd);

#define FLOCK_TEST_THREADS 24
#define FILENAME "lockfile.txt"
int main(int argc, char** argv){
    int pipesptc[2];
    int ret = pipe(pipesptc);
    if (0 != ret) {
        // failed open pipes
        printf("failed open pipe p to c\n");
        return 1;
    }
    int pipesctp[2];
    ret = pipe(pipesctp);
    if (0 != ret) {
        // failed open pipes
        printf("failed open pipe c to p\n");
        return 1;
    }
    pid_t p = fork();
    if(0 == p){
        // is child
        return child(pipesptc[0], pipesctp[1]);
    }else{
        return parent(pipesctp[0], pipesptc[1]);
    }
    // never here
    abort();
}

int wpipe(const char* name, int wfd, char* chr){
    printf("%s write pipe \"%s\"\n", name, chr);
    int ret = write(wfd, chr, 1);
    if(ret < 1){
        printf("%s write pipe failed: %s\n", name, strerror(errno));
        return 1;
    }
    fsync(wfd);
    printf("%s write pipe \"%s\" done\n", name, chr);
    return 0;
}
int quitpipe(const char* name, int wfd){
    return wpipe(name, wfd, "q");
}
int contpipe(const char* name, int wfd){
    return wpipe(name, wfd, "c");
}
int waitpipe(const char* name, int rfd){
    printf("%s waiting\n", name);
    char buf[1];
    int ret = read(rfd, buf, 1);
    if(1 > ret){
        printf("%s read fail: %s\n", name, strerror(errno));
        return 1;
    }
    printf("%s red: %c\n", name, buf[0]);
    switch(buf[0]){
        case 'c':
            printf("%s continue\n", name);
            return 0;
        case 'q':
            printf("%s recv quit\n", name);
            return 1;
    }
    abort();
}

static int locks = 0;

typedef struct flock_s {
    int fd;
} flock_t;
void* fill_thread_pools(flock_t* data){
    // this function will call cat_fs_flock to try fill thread pools.
    int ret = cat_fs_flock(data->fd, CAT_LOCK_EX | CAT_LOCK_NB);
    if(ret == 0){
        fprintf(stderr, "flock success, this is impossible\n");
        abort();
    };
    // if lock nb done, set count
    locks++;
    cat_fs_flock(data->fd, CAT_LOCK_EX);
    return NULL;
}

cat_bool_t req_done = cat_false;
void* access_request(void* _){
    // send any fs request (i.e. access)
    // thread pool should be still usable if implemention is correct
    cat_fs_access(FILENAME, F_OK);
    req_done = cat_true;
    return NULL;
}

int parent(int rfd, int wfd){
    printf("parent init cat\n");
    cat_init_all();
    cat_run(CAT_RUN_EASY);
    // open fd for flock
    cat_fs_unlink(FILENAME);
    int fd = cat_fs_open(FILENAME, CAT_FS_O_CREAT | CAT_FS_O_RDWR, 0666);
    if(fd < 0){
        // failed open file
        printf("parent failed open file: %s\n", cat_get_last_error_message());
        // tell child abort
        quitpipe("parent", wfd);
        return 1;
    }
    // tell child continue to lock file
    contpipe("parent", wfd);
    // wait child done
    if(waitpipe("parent", rfd)){
        return 1;
    }

    // try fill thread pool
    printf("try lock file to fill thread pool\n");

    flock_t data = {
        .fd = fd
    };
    for(int i=0; i < FLOCK_TEST_THREADS; i++){
        cat_coroutine_run(NULL, (void*)fill_thread_pools, &data);
    }

    // try do any fs operation using thread pool
    req_done = cat_false;
    cat_coroutine_run(NULL, access_request, NULL);

    int waittime;
    // sleep max 1.024s for access done
    for(waittime = 1; (!req_done) && (waittime < 1024); waittime*=2){
        cat_time_wait(waittime);
    }
    if(!req_done){
        fprintf(stderr, "thread pool full, deadlock occurred, please report a bug\n");
        abort();
    };
    printf("thread pool still usable\n");

    // sleep for all nb lock done
    while(locks < FLOCK_TEST_THREADS){
        if(waittime < 1024){
            waittime*=2;
        }
        cat_time_wait(waittime);
    }
    printf("all nb lock done, count %d\n", locks);

    // tell child continue to exit
    contpipe("parent", wfd);
    waitpipe("parent", rfd);

    cat_fs_close(fd);
    cat_fs_unlink(FILENAME);

    return 0;
}

int child(int rfd, int wfd){
    printf("child init cat\n");
    cat_init_all();
    cat_run(0);

    if(waitpipe("child", rfd)){
        return 1;
    }

    printf("child open %s\n", FILENAME);
    int fd = cat_fs_open(FILENAME, CAT_FS_O_CREAT | CAT_FS_O_RDWR, 0666);
    if(fd < 0){
        // we failed open target file
        printf("child failed open file %s: %s\n", FILENAME, cat_get_last_error_message());
        return 1;
    }

    printf("child lock file\n");
    cat_fs_flock(fd, CAT_LOCK_EX);
    if(contpipe("child", wfd)){
        return 1;
    }

    waitpipe("child", rfd);
    printf("child unlock file\n");
    cat_fs_flock(fd, CAT_LOCK_UN);
    cat_fs_close(fd);

    contpipe("child", wfd);
    // exit
    return 0;
}
