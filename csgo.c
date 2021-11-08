#define _GNU_SOURCE

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

#include "cor.h"

// csgo 的协程栈默认为8M
#define STACK_SIZE 8 * 1024 * 1024
thread_local G *tls_g;

Sched *sched;
void my_exit() { printf("shoule not happend \n\n"); }

void assert_m(int a, char *message) {
	if (a == 1) {
		sleep(1);
		printf("\nnassert failed %s \n ", message);
		exit(-1);
	}
}

// 将p绑定到当前M
void acquireP(P *p) {
	mtx_lock(&p->mu);
	uint old = 0;
	while (!atomic_compare_exchange_strong(&p->busy, &old, 1)) {
		printf("acquireP free to busy failed");
	}
	if (tls_g == NULL) {
		panic("acquireP tls_g is empty");
	}
	p->curm = tls_g->m;
	p->curm->curp = p;
	mtx_unlock(&p->mu);
}
static inline void bind_p_and_m(P *p, M *m) {
	mtx_lock(&p->mu);
	uint old = 0;
	while (!atomic_compare_exchange_strong(&p->busy, &old, 1)) {
		printf("acquireP free to busy failed");
	}
	if (tls_g == NULL) {
		panic("acquireP tls_g is empty");
	}
	p->curm = m;
	p->curm->curp = p;
	mtx_unlock(&p->mu);
}

static void _init_p(P *p) {
	init_glist(&p->g);
	atomic_store(&(p->busy), 0);
}

void runtimeinit_p(P **p, int *cpu_core_count) {
	int count = get_nprocs();
	count = 2;
	P *ptr;
	int i;
	*cpu_core_count = count;
	ptr = (P *)malloc(count * sizeof(P));
	*p = ptr;
	for (i = 0; i < count; i++) {
		_init_p(ptr + i);
	}
}

void init_glist(GList **head) {
	*head = (GList *)malloc(sizeof(GList));
	(*head)->node = NULL;
	(*head)->num = 0;
	mtx_t *mu_p = &((*head)->mu);
	mtx_init(mu_p, mtx_plain);
	return;
}

void lock_add_to_tail(GList *h, G *g) {
	assert(g != NULL);
	mtx_lock(&h->mu);
	GLink **t = &(h->node);
	while (*t != NULL) {
		t = &((*t)->next);
	}
	GLink *node = malloc(sizeof(GLink));
	node->g = g;
	node->next = NULL;
	*t = node;
	mtx_unlock(&h->mu);
	atomic_fetch_add(&h->num, 1);
}

struct G *lock_remove_head(GList *head) {
	assert(head != NULL);
	atomic_fetch_add(&head->num, -1);

	mtx_lock(&(head->mu));
	GLink *first = head->node;
	struct G *ret = NULL;
	if (first != NULL) {
		ret = first->g;
		head->node = first->next;
	}
	mtx_unlock(&(head->mu));

	return ret;
}

void csgo_exit() {
	// pid_t tid = syscall(SYS_gettid);
	if (tls_g == NULL) {
		pid_t tid = syscall(SYS_gettid);
		printf("csgo_exit333 threadId %d \n\n", tid);
		exit(-1);
	}
	assert_m(tls_g == NULL, "csgo_exit tls_g is null");
	tls_g->status = 1;
	// todo!!!  回收当前csgo 的栈信息， 目前存在内存泄露
	assert_m(tls_g == NULL, "csgo_exit tls_g is NULL\0");
	assert_m(tls_g->m == NULL, "csgo_exit tls_g->m is NULL\0");
	m_start(tls_g->m);
}

void park_fn(coroutinectx *ctx, G *curg) {
	struct G *g = malloc(sizeof(struct G));
	assert_m(curg != tls_g, "park_fn curg is not  tls_g");
	g->m = curg->m;
	g->status = 0;
	g->gctx = ctx;
	curg->status = 1;

	lock_add_to_tail(g->m->curp->g, g);
}

void runtime_park() {
	struct G *curg = tls_g;
	coroutinectx *ctx = malloc(sizeof(coroutinectx));
	runtimecall(ctx, curg, park_fn);
}

void malloc_stack(char **sp, char **bp) {
	char *p = (char *)malloc(STACK_SIZE);
	if (p == NULL) {
		printf("malloc_stack error \n");
		exit(-1);
	}
	*bp = p;
	p += STACK_SIZE;
	*sp = p;
	assert((*sp) - (*bp) == STACK_SIZE);
}

struct G *runtime_new_g(Funcval fn, struct M *m) {
	struct G *g = malloc(sizeof(struct G));
	g->m = m;
	g->status = '0';
	g->gctx = malloc(sizeof(coroutinectx));
	// rip 寄存器
	g->gctx->reg_ctx.registers[160 / 8 - 2] = (greg_t)(uintptr)fn.fn;
	char *sp = NULL;
	char *bp = NULL;
	malloc_stack(&sp, &bp);
	greg_t *exit_p = (greg_t *)(sp - 8);
	*exit_p = (greg_t)(uintptr)csgo_exit;
	sp = sp - 8;
	// movq  152(%rdi), %rbp
	// movq  168(%rdi), %rsp
	g->gctx->reg_ctx.registers[152 / 8 - 2] = (greg_t)(uintptr)bp;
	g->gctx->reg_ctx.registers[168 / 8 - 2] = (greg_t)(uintptr)sp;
	return g;
}

void runtimeinit_sched() {
	sched = malloc(sizeof(Sched));
	init_glist(&(sched->waitg));
	atomic_store(&sched->mcnt, 1); //包含程序主线程
}

static G *globalfindrunnable() { return lock_remove_head(sched->waitg); }

// 尝试从其他P中偷取
// 1. 如果有P为空闲, 则偷取一半
// 2. 如果不是空闲， 则偷一个
static inline G *strealg(P *curp) {
	// mtx_lock(&sched->mu);
	int i = 0;
	P *base = sched->p;
	P *cur;
	G *g;
	if (curp == base) {
		return NULL;
	}
	for (i = 0; i < sched->proc; i++) {
		cur = base + i;
		if (cur != curp && ((atomic_load(&cur->busy) == 0) ||
				    (atomic_load(&cur->g->num) > 0))) {
			assert_m(cur->g == NULL, "cur->g is NULL\0");
			g = lock_remove_head(cur->g);
			if (g != NULL) {
				g->m = curp->curm;
				printf("streal success G %p from %d to %d \n\n",
				       g, cur->curm->tid, curp->curm->tid);
				return g;
			}
		}
	}
	return NULL;
}

// 如果findrunnable 返回NULL, 那么schudule 应该直接返回，结束当前线程
// 如果curP == sched->p 则不能返回NULL 会导致全部线程都退出执行
static G *findrunnable(P *curp) {
	// M 的队列中找到一个可用的G
	int cnt = 0;
	int yield_cnt = 0;
top:
	cnt += 1;
	struct G *g = NULL;
	// assert_m(curp != tls_g->m->curp, "findrun curp is not
	// tls_g->-m->g\0");
	g = lock_remove_head(curp->g);
	if (g == NULL) {
		// g = globalfindrunnable();
		if (g == NULL) {
			g = strealg(curp);
			if (g == NULL) {
				if (cnt % 3 == 0) {
					yield_cnt += 1;
					int ret = sched_yield();
					if (ret != 0) {
						printf("sched_yield failed %d ",
						       ret);
						panic("sched_yield failed");
					}
					if (yield_cnt == 5) {
						return NULL;
					}
				}
				goto top;
			}
		}
	}
	return g;
}

static inline void init_m(M **_m) {
	M *m = (M *)malloc(sizeof(M));
	m->save_ctx = malloc(sizeof(coroutinectx));
	m->curp = NULL;
	m->tid = 0;
	m->thread_exit = malloc(sizeof(coroutinectx));
	m->exit_flag = 0;
	*_m = m;
}

int cus_thread_entry(void *argv) {
	M *m = (M *)argv;
	pid_t tid = syscall(SYS_gettid);
	m->tid = tid;
	tls_g = NULL;

	// 保存当前上下文，用于退出thread 上下文
	// 退出时栈里面的内容全部丢失
	csgosave(m->thread_exit);
	if (m->exit_flag == 0) {
		atomic_fetch_add(&sched->mcnt, 1);
		return m_start(argv);
	}
	pid_t tid2 = syscall(SYS_gettid);
	assert(tid == tid2);
	printf("********************* thread %d exit \n", tid2);
	atomic_fetch_add(&sched->mcnt, -1);
	// TODO 如果是主线程还需要 thrd_join() 其他线程，然后才退出

	return 0;
}

static inline void exit_thread(M *m) {
	m->exit_flag = 1;
	csgogo(m->thread_exit);
}

void newm(M *m) {
	/**
	 * WARNING 这里有个大坑啊，直接使用clone 函数创建的线程， 没法使用
	 * thread_local 变量
	 *
	 */
	m->ths = (thrd_t *)malloc(sizeof(thrd_t));
	int ret = thrd_create(m->ths, cus_thread_entry, m);
	/*
	int32 clone_flags = CLONE_VM
			    | CLONE_FS
			    | CLONE_FILES   /* share fd
					       table
			    | CLONE_SIGHAND /* share sig
					       handlerp
					       table
			    | CLONE_THREAD;
	char *stack_sp = NULL;
	char *bp = NULL;
	malloc_stack(&stack_sp, &bp);
	atomic_fetch_add(&sched->mcnt, 1);
	int ret = clone(cus_thread_entry, stack_sp, clone_flags, m);
	if (ret < 0) {
		printf(
		    "init_m clone "
		    "create pthread "
		    "error %d \n",
		    ret);
		exit(-1);
	}
	*/
}

// 1. 当前等待调度的G 超过1个
// 2. 有空闲的P
// 3. 没有处于空闲的M // todo
// 目前还没实现，需要加入更过的状态控制
static inline void try_new_m() {
	int count = sched->proc;
	int i;
	int free_p = 0;
	int local_wait_g = 0;
	P *base = sched->p;
	P *cur = NULL;
	G *g = tls_g;
	assert_m(g == NULL, "try_new_m g == null\0");
	assert_m(g->m == NULL, "try_new_m g->m == null\0");
	assert_m(g->m->curp == NULL, "try_new_m g->m->curp == null\0");
	GLink *list = g->m->curp->g->node;
	while (list != NULL) {
		local_wait_g += 1;
		list = list->next;
	}
	if (local_wait_g < 1) {
		return;
	}
	M *nm = NULL;
	for (i = 0; i < count; i++) {
		cur = base + i;
		if (atomic_load(&cur->busy) == 0) {
			M *nm;
			init_m(&nm);
			bind_p_and_m(cur, nm);
			printf("newm start to create a new thread");
			newm(nm);
			return;
		}
	}
}

int m_start(void *argv) {
	M *m = (M *)argv;
	int i;
	int count = sched->proc;
	P *base = sched->p;
	P *cur;
	G *g;
TRY:
	g = findrunnable(m->curp);
	// 主线程不能先与子线程退出
	// || (atomic_load(&sched->mcnt) == 1)
	if (g == NULL &&
	    ((m->curp != sched->p) || (atomic_load(&sched->mcnt) == 1))) {
		m->curp->curm = NULL;
		cur = m->curp;
		m->curp = NULL;
		atomic_store(&cur->busy, 0);
		// 该函数不会返回直接 jmp cus_thread_entry 退出函数的地方
		exit_thread(m);
		return 0;
	} else if (g == NULL) {
		if (atomic_load(&sched->mcnt) == 1) {
			int cnt = atomic_load(&m->curp->g->num);
			sleep(2);
		}
		assert(m->tid == 0);
		goto TRY;

		exit(-1);
	}
	assert(g != NULL);
	tls_g = g;
	assert_m(m != tls_g->m, "m_start m is not tls_g->m55555343535 \0\n");
	g->m = m;
	try_new_m();
	csgosave_and_swap(m->save_ctx, g);
	panic("should not run ");
	return 0;
}

void runqput(P *p, G *gp) { lock_add_to_tail(p->g, gp); }

// 1. init sched
// 2. init p
// 新建初始化m0 和g0 用于 执行entry函数
int runtimestart(void (*entry)()) {
	runtimeinit_sched();
	runtimeinit_p(&(sched->p), &(sched->proc));
	printf("init_p success\n");
	Funcval fn;
	fn.fn = entry;
	M *m0;
	init_m(&m0);

	struct G *g = runtime_new_g(fn, m0);
	tls_g = g;
	runqput(sched->p, g);
	acquireP(sched->p); // bind P 和当前M

	pid_t tid = syscall(SYS_gettid);
	assert_m(m0 != tls_g->m, "runtimestart m_start m is not tls_g->m");

	csgosave(m0->thread_exit);
	if (m0->exit_flag == 0) {
		m_start(m0);
		return -1;
	}
	printf("main thread exit(0) \n");
	return 0;
}
