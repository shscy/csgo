#define _GNU_SOURCE
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <wait.h>

#include <stdlib.h>

#define SIZE 5

thread_local int cnt = 0;
int run(void *argv){
    cnt = 10;
    int *f = (int*)argv;
    if(*f == 100){
        cnt += 100;
    }else{
        cnt += 1 ;
    }
    pid_t tid = syscall(SYS_gettid);
    printf("\nthread %d cnt %d \n", tid, cnt);
    return 0;
}

void create_th(int* flag){
    int32_t clone_flags = CLONE_VM	    /* share memory */
			    | CLONE_FS	    /* share cwd, etc */
			    | CLONE_FILES   /* share fd
					       table */
			    | CLONE_SIGHAND /* share sig
					       handler
					       table */
			    | CLONE_THREAD;
	char* stack_sp = (char*)malloc(4*1024*1024);
    stack_sp += 4*1024*1024;
	int ret = clone(run, stack_sp, clone_flags, flag);
	if (ret < 0) {
		printf(
		    "init_m clone "
		    "create pthread "
		    "error %d \n",
		    ret);
		exit(-1);
	}
}
int main(){

     thrd_t id[SIZE];
    int arr[SIZE] = {100, 1};

    /* create 5 threads. */
    for(int i = 0; i < 2; i++) {
        thrd_create(&id[i], run, &arr[i]);
    }

    /* wait for threads to complete. */
    for(int i = 0; i < 2; i++) {
        thrd_join(id[i], NULL);
    }

    printf("main thread val: %d\n", cnt);
    // int*f1= (int*)malloc(sizeof(int));
    // int* f2 = (int*)malloc(sizeof(int));
    // *f1 = 100;
    // *f2 = 1;
    // create_th(f1);
    // create_th(f2);
    // sleep(1);
    // return 0;
}
