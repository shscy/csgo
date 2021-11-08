#ifndef cor_h

#define cor_h
#include <stdatomic.h>
#include <sys/types.h>
#include <threads.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>

typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed int int32;
typedef unsigned int uint32;
typedef signed long long int int64;
typedef unsigned long long int uint64;
typedef float float32;
typedef double float64;

typedef uint64 uintptr;
typedef int64 intptr;

typedef signed long long int greg_t;

#define REG_NUM 23

typedef struct corstack {
	uintptr sp;
	uintptr bp;
} corstack;

typedef struct corregister {
	// 23个寄存器的值都需要存储
	// https://cs61.seas.harvard.edu/site/2019/Asm/#Registers
	greg_t registers[REG_NUM];
} corregister;

typedef struct coroutinectx {
	corstack stack;
	corregister reg_ctx;
} coroutinectx;

typedef struct Funcval {
	void (*fn)(void);
	// variable-size, fn-specific data here
} Funcval;

typedef struct G G;
typedef struct M M;
typedef struct P P;

typedef struct GLink {
	G* g;
	struct GLink* next;
} GLink;

typedef struct GList {
	mtx_t mu;
	GLink* node;
	atomic_int num;
} GList;

struct M {
	// struct G* link;
	// todo 删除link字段， 汇编中写死了偏移量。。。后面再想办法了
	GList* unless_link;
	coroutinectx* save_ctx;
	P* curp;
	pid_t tid;
	thrd_t* ths;
	coroutinectx* thread_exit; // jmp到该地址用于退出当前thread
	int exit_flag; // 当为1时表示退出
};

struct G {
	coroutinectx* gctx;  // 8 world
	M* m;		     // 8个 world
	uint8 status;
};

// P 会盗取其他P的G
// 因此还是需要加锁
// todo 后续考虑建立两个链表
// 一个链表不用加锁用于M自己操作
// 另一个链表用于盗取其他队列的G
struct P {
	mtx_t mu;
	GList* g;
	M* curm;
	atomic_uint busy;  // 0 is free, 1 is busy
};

typedef struct Sched {
	mtx_t mu;
	GList* waitg;
	P* p;
	int proc;	  // cpu core count
	atomic_int mcnt;  //活跃的M的数量
} Sched;

// 初始化入口
int runtimestart(void (*entry)());
void runtimeinit_sched();
// 用于初始化数组p
void runtimeinit_p(P** p, int* cpu_core_count);
void acquireP(P* p);

void init_glist(GList** head);
void lock_add_to_tail(GList* head, struct G* g);
struct G* lock_remove_head(GList* head);

int csgosave(coroutinectx* ctx);
int csgogo(coroutinectx* ctx);

// 保存当前上下文到ctx中，然后切换为g
int csgosave_and_swap(coroutinectx* ctx, struct G* g);

// static inline void init_m(M *m);
struct G* runtime_new_g(Funcval fn, struct M* m);
void runtimecall(coroutinectx* ctx, struct G* g,
		 void (*park_fn)(coroutinectx* ctx, struct G* curg));
int m_start(void* argv);
void csgo_exit();
void runtime_park();
void runqput(P* p, G* gp);
void panic(char*);

#endif
