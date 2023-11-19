
/*
    The test info block is for mp_tester.php to check if the result is good.
    the mark below is for tester to find the json below.
    here is TEST_INFO_BLOCK
    {
        "marks": [
            "all nb lock done, count",
            "thread pool still usable"
        ],
        "timeout": 10
    }
*/

// manually build
// cl /I ..\..\deps\libuv\include /I ..\..\deps\libuv\include\uv /I ..\..\include ..\..\build\Debug\cat.lib /MDd kernel32.lib ws2_32.lib user32.lib advapi32.lib iphlpapi.lib psapi.lib userenv.lib flock_windows.c
// manually run
// flock_windows.exe

#include "cat.h"
#include "cat_fs.h"

#define FLOCK_TEST_THREADS 24

int parent(int argc, wchar_t**argvw, HANDLE hIn, HANDLE hOut);
int child(int argc, wchar_t**argvw, HANDLE hIn, HANDLE hOut);
#define FILENAME "lockfile.txt"
int wmain(int argc, wchar_t**argvw){
    int is_child = 0;
    if(argc > 1){
        if(wcsncmp(argvw[1], L"child", 6) == 0){
            is_child = 1;
        }
        if(argc < 4){
            printf("bad io handles for child\n");
            return 1;
        }
    }

    if (is_child){
        printf("child start\n");
        HANDLE hPtcR, hCtpW;
        swscanf(argvw[2], L"%zu", &hPtcR);
        swscanf(argvw[3], L"%zu", &hCtpW);
        wprintf(L"dasda %p\n", hCtpW);
        int ret = child(argc, argvw, hPtcR, hCtpW);
        printf("child self exiting\n");
        CloseHandle(hCtpW);
        return ret;
    }else{
        HANDLE hPtcR, hPtcW, hCtpR, hCtpW;
        SECURITY_ATTRIBUTES sa = {
            .bInheritHandle = TRUE,
            .lpSecurityDescriptor = NULL,
            .nLength = sizeof(sa)
        };
        BOOL retb = CreatePipe(
            &hPtcR,
            &hPtcW,
            &sa,
            0
        );
        if(!retb){
            printf("parent to child pipe failed to create: CreatePipe 0x%08x\n", GetLastError());
            return 1;
        }
        retb = CreatePipe(
            &hCtpR,
            &hCtpW,
            &sa,
            0
        );
        if(!retb){
            printf("child to parent pipe failed to create: CreatePipe 0x%08x\n", GetLastError());
            return 1;
        }

        // create child
        wchar_t cmd[4096];
        swprintf(cmd, 4096, L"%s child %zu %zu", argvw[0], (uint64_t)hPtcR, (uint64_t)hCtpW);
        STARTUPINFOW si = {
            .cb = sizeof(si),
            .hStdError = GetStdHandle(STD_ERROR_HANDLE),
            .hStdInput = GetStdHandle(STD_INPUT_HANDLE),
            .hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE),
        };
        PROCESS_INFORMATION pi = {0};
        SECURITY_ATTRIBUTES tsa = {
            .nLength = sizeof(tsa),
            .bInheritHandle = TRUE,
            .lpSecurityDescriptor = NULL
        };
        SECURITY_ATTRIBUTES psa = {
            .nLength = sizeof(psa),
            .bInheritHandle = TRUE,
            .lpSecurityDescriptor = NULL
        };

        wprintf(L"executing \"%s\"\n", cmd);
        retb = CreateProcessW(NULL, cmd, &psa, &tsa, TRUE, 0, NULL, NULL, &si, &pi);
        if(!retb){
            // failed create process
            printf("CreateProcessW failed 0x%08d\n", GetLastError());
            goto fail;
        }
        int ret = parent(argc, argvw, hCtpR, hPtcW);
        if (ret) {
            printf("parent failed, killing child\n");
            retb = TerminateProcess(pi.hProcess, 0);
            if(!retb){
                // failed kill child
                printf("TerminateProcess failed 0x%08d\n", GetLastError());
                goto fail;
            }
        }
        return ret;
        fail:
        CloseHandle(hPtcW);
        return 1;
    }
}

int wpipe(const char * name, HANDLE hPipe, char* chr){
    printf("%s write pipe \"%s\"\n", name, chr);
    DWORD written = 0;
    BOOL retb = WriteFile(hPipe, chr, 1, &written, NULL);
    if(!retb || written != 1){
        printf("%s WriteFile failed: 0x%08x\n", name, GetLastError());
        return -1;
    }
    printf("%s write pipe \"%s\" done\n", name, chr);
    return 0;
}
int quitpipe(const char* name, HANDLE hPipe){
    return wpipe(name, hPipe, "q");
}
int contpipe(const char* name, HANDLE hPipe){
    return wpipe(name, hPipe, "c");
}
int waitpipe(const char* name, HANDLE hPipe){
    printf("%s waiting\n", name);
    char buf[1];
    DWORD red = 0;
    BOOL retb = ReadFile(hPipe, buf, 1, &red, NULL);
    if(!retb || red != 1){
        printf("%s ReadFile failed: 0x%08x\n", name, GetLastError());
        return -1;
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
    int ret = cat_fs_flock(data->fd, CAT_FS_FLOCK_FLAG_EXCLUSIVE | CAT_FS_FLOCK_FLAG_NONBLOCK);
    if(ret == 0){
        printf("flock success, this is impossible\n");
        abort();
    };
    // if lock nb done, set count
    locks++;
    cat_fs_flock(data->fd, CAT_FS_FLOCK_FLAG_EXCLUSIVE);
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

int parent(int argc, wchar_t**argvw, HANDLE hIn, HANDLE hOut){
    printf("parent init cat\n");
    cat_init_all();
    cat_run(0);

    printf("parent unlink file %s\n", FILENAME);
    cat_fs_unlink(FILENAME);
    printf("parent open file %s\n", FILENAME);
    int fd = cat_fs_open(FILENAME, CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0666);
    if(fd < 0){
        // we failed open target file
        printf("parent failed open file %s: %s\n", FILENAME, cat_get_last_error_message());
        quitpipe("parent", hOut);
        return 1;
    }
    // tell child continue to lock file
    contpipe("parent", hOut);
    // wait child done
    if(waitpipe("parent", hIn)){
        return 1;
    }

    // try fill thread pool
    printf("try lock file to fill thread pool\n");

    flock_t data = {
        .fd = fd
    };
    for(int i=0; i < FLOCK_TEST_THREADS; i++){
        cat_coroutine_run(NULL, fill_thread_pools, &data);
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
        printf("thread pool full, deadlock occurred, please report a bug\n");
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

    cat_fs_unlink(FILENAME);

    // tell child continue to exit
    contpipe("parent", hOut);
    waitpipe("parent", hIn);

    return 0;
}

int child(int argc, wchar_t**argvw, HANDLE hIn, HANDLE hOut){
    printf("child init cat\n");
    cat_init_all();
    cat_run(0);

    if(waitpipe("child", hIn)){
        return 1;
    }

    printf("child open %s\n", FILENAME);
    int fd = cat_fs_open(FILENAME, CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0666);
    if(fd < 0){
        // we failed open target file
        printf("child failed open file %s: %s\n", FILENAME, cat_get_last_error_message());
        return 1;
    }

    printf("child lock file\n");
    cat_fs_flock(fd, CAT_FS_FLOCK_FLAG_EXCLUSIVE);
    if(contpipe("child", hOut)){
        return 1;
    }

    waitpipe("child", hIn);
    printf("child unlock file\n");
    cat_fs_flock(fd, CAT_FS_FLOCK_FLAG_UNLOCK);
    cat_fs_close(fd);

    contpipe("child", hOut);

    return 0;
}
