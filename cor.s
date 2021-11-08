    .text
    .global csgosave
    .type csgosave @function
    .global csgogo
    .type csgogo @function 
    .global csgosave_and_swap
    .type csgosave_and_swap @function
    .global allctx
    .type allctx @function 
    .global runtimecall
    .type runtimecall @function 
    .global m_start
    .type m_start @function 

    .global panic 
    .type panic @function 
    .global mu_exit 
    .type mu_exit @function 

// 第一个参数ctx 第二个是g
csgosave_and_swap:
    pushq %rsi  
    pushq %rdi  
    callq csgosave
    popq %rdi 
    /* 将rip 的值指向LL, csgosave默认保存的rip是 上面的popq%rdi 指令，*/
    /* 这里要强制进行LL 因此要注意在LL中popq掉栈中的值 */
    lea LL(%rip), %rax 
    movq %rax, 160(%rdi) 
    /*把g作为csggo的参数 label1 获取g的gctx参数*/ 
    popq %rax 
    movq (%rax), %rdi 
    callq csgogo 
    /*csgoroutine 不该执行这里来*/
    callq my_exit
    ret 
LL:
    callq my_exit
    popq %rdi
    popq %rdi
    ret 

csgogo:
    movq  16(%rdi), %rax 
    movq  24(%rdi), %rbx 
    movq  32(%rdi), %rcx 
    movq  40(%rdi), %rdx 
    movq  48(%rdi), %rsi 
    movq  64(%rdi), %r8 
    movq  72(%rdi), %r9 
    movq  80(%rdi), %r10 
    movq  88(%rdi), %r11
    movq  96(%rdi), %r12
    movq  104(%rdi), %r13 
    movq  112(%rdi), %r14 
    movq  120(%rdi), %r15 
    movq  128(%rdi), %rsp 
    movq  136(%rdi), %rbp 
    movq  144(%rdi), %r12 
    movq  152(%rdi), %rbp 
    movq  168(%rdi), %rsp 
    /*rip*/
    movq 160(%rdi) , %rax
    jmp *%rax 


csgosave:    
    // goroutine gogosave do not need to update sp and bp pointer 
    // just save register 
    movq %rax, 16(%rdi)
    movq %rbx, 24(%rdi)
    movq %rcx, 32(%rdi)
    movq %rdx, 40(%rdi)
    movq %rsi, 48(%rdi)
    // movq %rdi, 56(%rdi)
    movq %r8, 64(%rdi)
    movq %r9, 72(%rdi)
    movq %r10, 80(%rdi)
    movq %r11, 88(%rdi)
    movq %r12, 96(%rdi)
    movq %r13, 104(%rdi)
    movq %r14, 112(%rdi)
    movq %r15, 120(%rdi)
    movq %rsp, 128(%rdi)
    movq %rbp, 136(%rdi)
    movq %r12, 144(%rdi)
    movq %rbp, 152(%rdi)

    movq (%rsp), %rcx

    movq    %rcx, 160(%rdi)
    leaq    8(%rsp), %rcx                /* Exclude the return address.  */
    movq    %rcx, 168(%rdi)
    movq    $0x0, %r10
    movq    $0, %rax 
    ret 

/*第一个参数是ctx 第二个参数为 curg 第三参数为 fn (coroutinectx* ctx, struct G*curg) */
runtimecall:
    callq csgosave
    pushq %rdi 
    pushq %rsi 
    callq  park_fn
    popq %rsi 
    popq %rdi 
    lea rcallLL(%rip), %rax 
    movq %rax, 160(%rdi) 
    lea m_start(%rip), %rax 
    /*获取m然后赋值为rdi 然后curg让出执行权, 执行m_start 注意当前堆栈还是在curg上，没有像golang一样切换到g0栈上*/
    movq 8(%rsi), %rdi  
    jmp *%rax 
    /* 修改ctx的rip 指向为函数*/
rcallLL:
    callq my_exit
    ret
