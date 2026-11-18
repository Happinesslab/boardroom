/* Entry from the game's day-end routine (FL_2027.exe RVA 0xF17160), whose
 * first 16 bytes the app replaces with `jmp [rip+0]; dq hook`. Runs the
 * requests, then the 16 displaced prologue bytes (all position-independent:
 * push rbp/rbx/rsi/rdi/r12-r15; mov rbp,rsp) and jumps back to RVA 0xF17170.
 *
 * Layout of the blob (offsets fixed, the app relies on them):
 *   0x000 hook entry
 *   0x080 ctx pointer (filled in by the app)
 *   0x088 address to return to (RVA 0xF17170, filled in by the app)
 *   0x100 run_requests (live_stub.c)
 */
    .intel_syntax noprefix
    .text
    .globl hook
hook:
    push rcx
    push rdx
    push r8
    push r9
    sub rsp, 0x28                 /* shadow space; rsp is 16-aligned again */
    mov rdx, rcx                  /* manager */
    mov rcx, qword ptr [rip + ctx]
    call run_requests
    add rsp, 0x28
    pop r9
    pop r8
    pop rdx
    pop rcx
    /* displaced prologue of day_end */
    push rbp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov rbp, rsp
    jmp qword ptr [rip + back]

    .balign 0x80, 0xCC
ctx:  .quad 0
back: .quad 0
    .balign 0x100, 0xCC
