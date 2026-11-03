#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <time.h>
#include <sys/times.h>

// 1. Memory Segments
const char *rodata_var = "I live in .rodata (Read-Only)";
int initialized_global = 42;          // .data
int uninitialized_global;             // .bss
void code_function(void) {}           // .text

// Jump buffer to survive touching the kernel upper half
static sigjmp_buf jump_buffer;

// Hardware exception handler (intercepts Ring 3 -> Ring 0 page fault signal)
void segfault_handler(int sig, siginfo_t *info, void *ucontext) {
    printf("   [!] HARDWARE TRAP INTERCEPTED!\n");
    printf("   [!] Fault Address: %p\n", info->si_addr);
    printf("   [!] MMU Enforcement: Supervisor-only bit set or unmapped. Access denied.\n");
    siglongjmp(jump_buffer, 1);
}

// Translate Virtual Address -> Physical RAM Frame via /proc/self/pagemap
uint64_t get_physical_pfn(void *virtual_addr) {
    int pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (pagemap_fd < 0) return 0;

    uintptr_t vaddr = (uintptr_t)virtual_addr;
    uint64_t page_num = vaddr / 4096;
    off_t offset = page_num * sizeof(uint64_t);

    uint64_t entry = 0;
    if (pread(pagemap_fd, &entry, sizeof(entry), offset) != sizeof(entry)) {
        close(pagemap_fd);
        return 0;
    }
    close(pagemap_fd);

    // Bit 63 indicates if page is present in physical RAM
    if (!(entry & (1ULL << 63))) return 0;

    // Bits 0-54 contain the Page Frame Number (PFN)
    uint64_t pfn = entry & 0x007FFFFFFFFFFFFFULL;
    return pfn;
}

// Read integer values from /proc sysctl tunables
long read_sysctl(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long val = 0;
    if (fscanf(f, "%ld", &val) != 1) val = -1;
    fclose(f);
    return val;
}

int main(void) {
    int stack_var = 99;
    int *heap_var = malloc(sizeof(int));
    *heap_var = 123; // Touch memory to force physical page allocation

    printf("=================================================================\n");
    printf(" 1. VIRTUAL MEMORY LAYOUT (Lower Half: User Space)\n");
    printf("=================================================================\n");
    printf(" .text   (Code segment)        : %p\n", (void *)code_function);
    printf(" .rodata (Constants)           : %p\n", (void *)rodata_var);
    printf(" .data   (Initialized globals) : %p\n", (void *)&initialized_global);
    printf(" .bss    (Uninitialized globals): %p\n", (void *)&uninitialized_global);
    printf(" [heap]  (Dynamic allocations) : %p\n", (void *)heap_var);
    printf(" [stack] (Local variables)     : %p\n", (void *)&stack_var);

    printf("\n=================================================================\n");
    printf(" 2. VIRTUAL TO PHYSICAL TRANSLATION (MMU Mapping)\n");
    printf("=================================================================\n");
    uint64_t pfn = get_physical_pfn(heap_var);
    if (pfn) {
        printf(" Heap Virtual Address  : %p\n", (void *)heap_var);
        printf(" Physical Frame (PFN)  : 0x%lx\n", pfn);
        printf(" Physical RAM Address  : 0x%lx\n", (pfn * 4096) + ((uintptr_t)heap_var & 0xFFF));
    } else {
        printf(" Physical translation requires root (CAP_SYS_ADMIN) on modern Linux.\n");
        printf(" (Run with sudo to inspect physical PFNs).\n");
    }

    printf("\n=================================================================\n");
    printf(" 3. HIGHER-HALF KERNEL PROBE (Ring 3 vs Ring 0 Boundary)\n");
    printf("=================================================================\n");
    // Hook SIGSEGV to catch the hardware violation
    struct sigaction sa;
    sa.sa_sigaction = segfault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);

    // Common x86_64 higher-half direct physical map base
    volatile char *kernel_upper_half = (volatile char *)0xFFFF888000000000ULL;
    printf(" Attempting to read from Kernel Upper Half: %p\n", (void *)kernel_upper_half);

    if (sigsetjmp(jump_buffer, 1) == 0) {
        // Meltdown-style illicit read attempt from User Mode
        char val = *kernel_upper_half;
        printf(" Unreachable! Read byte: %d\n", val);
    } else {
        printf(" -> Successfully survived hardware fault. Kernel memory remains protected.\n");
    }

    printf("\n=================================================================\n");
    printf(" 4. SCHEDULER: JIFFIES, LATENCY & GRANULARITY\n");
    printf("=================================================================\n");
    long clock_ticks = sysconf(_SC_CLK_TCK);
    long target_latency = read_sysctl("/proc/sys/kernel/sched_latency_ns");
    long min_granularity = read_sysctl("/proc/sys/kernel/sched_min_granularity_ns");

    printf(" Kernel Jiffy Frequency (HZ) : %ld ticks/second (1 tick = %.2f ms)\n",
           clock_ticks, 1000.0 / clock_ticks);
    if (target_latency > 0) {
        printf(" CFS Target Latency          : %ld ns (%.2f ms)\n",
               target_latency, target_latency / 1e6);
        printf(" CFS Min Granularity         : %ld ns (%.2f ms)\n",
               min_granularity, min_granularity / 1e6);
        printf(" Max tasks before exceeding target latency: %ld\n",
               target_latency / min_granularity);
    } else {
        printf(" /proc/sys/kernel/sched_* tunables abstracted on this kernel version.\n");
    }

    // Measure scheduling timeslice burn
    struct tms t_start, t_end;
    clock_t start_tick = times(&t_start);
    volatile uint64_t spin = 0;
    while (spin < 300000000) spin++; // Burn CPU cycles
    clock_t end_tick = times(&t_end);

    printf(" CPU Work Finished           : Consumed %ld jiffies (~%.2f ms user time)\n",
           (t_end.tms_utime - t_start.tms_utime),
           ((t_end.tms_utime - t_start.tms_utime) * 1000.0) / clock_ticks);

    printf("=================================================================\n");
    free(heap_var);
    return 0;
}
