#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include "cor.h"

extern thread_local G *tls_g;

void function2() {
	static int cnt = 0;
	pid_t tid = syscall(SYS_gettid);
	cnt += 1;
	printf("\n\n[threadId %d] function2 ping pong ping pong %d \n\n", tid,
	       cnt);
}

void append_func() {
	Funcval fn;
	fn.fn = function2;
	G *g = runtime_new_g(fn, tls_g->m);
	runqput(tls_g->m->curp, g);
}

void function1() {
	static int a = 0;
	a = a + 1;
	pid_t tid;
	int c = 0;

Top:
	tid = syscall(SYS_gettid);
	printf(
	    "\n[threadId %d]function1  stage11111111111 start  to run   %d \n",
	    tid, a);
	for (c = 0; c < 10; c++) {
		append_func();
	}

	// 让出执行权，开始调用function2
	runtime_park();
	tid = syscall(SYS_gettid);
	printf("thread[%d] function1  stage22222222222 start  to run   %d \n",
	       tid, a);
	a = a + 1;
}

int main(int argc, char **argv) { return runtimestart(function1); }

void panic(char *mess) {
	printf("\nshould not happen: \n");
	exit(-5);
}
