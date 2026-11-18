/*
 * UTDeck stage 1.2 step 3: trace a program's KGSL (Qualcomm GPU driver) ioctls with ptrace.
 * Works for any userspace driver (Turnip, or the vendor driver through libhybris), because it
 * watches system calls, not libraries. Follows all threads and child processes.
 *
 *   kgsl-trace [-o LOG] [-q] -- PROGRAM ARGS...
 *     -q  quiet: no line per submission, only context creation, properties and the summary
 *
 * Decodes: context creation (flags: priority, preemption style, type), set-property, and command
 * submissions (IOCTL_KGSL_GPU_COMMAND: flags, command buffers and their sizes, timestamp). Counts
 * every other KGSL ioctl by number. arm64 only.
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <asm/ptrace.h>

#define NR_IOCTL 29

/* KGSL ABI (include/uapi/linux/msm_kgsl.h). */
struct drawctxt_create { uint32_t flags, drawctxt_id; };
struct getproperty { uint32_t type; uint32_t pad; uint64_t value; uint64_t sizebytes; };
struct gpu_command {
    uint64_t flags, cmdlist; uint32_t cmdsize, numcmds;
    uint64_t objlist; uint32_t objsize, numobjs;
    uint64_t synclist; uint32_t syncsize, numsyncs, context_id, timestamp;
};
struct command_object { uint64_t offset, gpuaddr, size; uint32_t flags, id; };

static FILE *out;
static int quiet;
static double t0;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static int peek(pid_t pid, uint64_t addr, void *buf, size_t len)
{
    struct iovec l = { buf, len }, r = { (void *)addr, len };
    return process_vm_readv(pid, &l, 1, &r, 1, 0) == (ssize_t)len ? 0 : -1;
}

static const char *ioctl_name(unsigned nr)
{
    switch (nr) {
    /* Generated from msm_kgsl.h (the KGSL ioctl numbers). */
    case 0x02: return "DEVICE_GETPROPERTY";
    case 0x06: return "DEVICE_WAITTIMESTAMP";
    case 0x07: return "DEVICE_WAITTIMESTAMP_CTXTID";
    case 0x10: return "RINGBUFFER_ISSUEIBCMDS";
    case 0x11: return "CMDSTREAM_READTIMESTAMP_OLD/CMDSTREAM_READTIMESTAMP";
    case 0x12: return "CMDSTREAM_FREEMEMONTIMESTAMP/CMDSTREAM_FREEMEMONTIMESTAMP_OLD";
    case 0x13: return "DRAWCTXT_CREATE";
    case 0x14: return "DRAWCTXT_DESTROY";
    case 0x15: return "MAP_USER_MEM";
    case 0x16: return "CMDSTREAM_READTIMESTAMP_CTXTID";
    case 0x17: return "CMDSTREAM_FREEMEMONTIMESTAMP_CTXTID";
    case 0x20: return "SHAREDMEM_FROM_PMEM";
    case 0x21: return "SHAREDMEM_FREE";
    case 0x22: return "DRAWCTXT_BIND_GMEM_SHADOW";
    case 0x23: return "SHAREDMEM_FROM_VMALLOC";
    case 0x24: return "SHAREDMEM_FLUSH_CACHE";
    case 0x25: return "DRAWCTXT_SET_BIN_BASE_OFFSET";
    case 0x2E: return "CMDWINDOW_WRITE";
    case 0x2F: return "GPUMEM_ALLOC";
    case 0x30: return "CFF_SYNCMEM";
    case 0x31: return "CFF_USER_EVENT/TIMESTAMP_EVENT_OLD";
    case 0x32: return "SETPROPERTY";
    case 0x33: return "TIMESTAMP_EVENT";
    case 0x34: return "GPUMEM_ALLOC_ID";
    case 0x35: return "GPUMEM_FREE_ID";
    case 0x36: return "GPUMEM_GET_INFO";
    case 0x37: return "GPUMEM_SYNC_CACHE";
    case 0x38: return "PERFCOUNTER_GET";
    case 0x39: return "PERFCOUNTER_PUT";
    case 0x3A: return "PERFCOUNTER_QUERY";
    case 0x3B: return "PERFCOUNTER_READ";
    case 0x3C: return "GPUMEM_SYNC_CACHE_BULK";
    case 0x3D: return "SUBMIT_COMMANDS";
    case 0x40: return "SYNCSOURCE_CREATE";
    case 0x41: return "SYNCSOURCE_DESTROY";
    case 0x42: return "SYNCSOURCE_CREATE_FENCE";
    case 0x43: return "SYNCSOURCE_SIGNAL_FENCE";
    case 0x44: return "CFF_SYNC_GPUOBJ";
    case 0x45: return "GPUOBJ_ALLOC";
    case 0x46: return "GPUOBJ_FREE";
    case 0x47: return "GPUOBJ_INFO";
    case 0x48: return "GPUOBJ_IMPORT";
    case 0x49: return "GPUOBJ_SYNC";
    case 0x4A: return "GPU_COMMAND";
    case 0x4B: return "PREEMPTIONCOUNTER_QUERY";
    case 0x4C: return "GPUOBJ_SET_INFO";
    case 0x50: return "SPARSE_PHYS_ALLOC";
    case 0x51: return "SPARSE_PHYS_FREE";
    case 0x52: return "SPARSE_VIRT_ALLOC";
    case 0x53: return "SPARSE_VIRT_FREE";
    case 0x54: return "SPARSE_BIND";
    case 0x55: return "GPU_SPARSE_COMMAND";
    case 0x56: return "GPUMEM_BIND_RANGES";
    case 0x57: return "GPU_AUX_COMMAND";
    case 0x58: return "TIMELINE_CREATE";
    case 0x59: return "TIMELINE_WAIT";
    case 0x5A: return "TIMELINE_QUERY";
    case 0x5B: return "TIMELINE_SIGNAL";
    case 0x5C: return "TIMELINE_FENCE_GET";
    case 0x5D: return "TIMELINE_DESTROY";
    case 0x5E: return "GET_FAULT_REPORT";
    case 0x5F: return "RECURRING_COMMAND";
    case 0x60: return "READ_CALIBRATED_TIMESTAMPS";
    default: return NULL;
    }
}

static void ctx_flags(uint32_t f, char *s, size_t n)
{
    static const char *bits[] = { "SAVE_GMEM", "NO_GMEM_ALLOC", "SUBMIT_IB_LIST", "CTX_SWITCH", "PREAMBLE",
                                  "TRASH_STATE", "PER_CONTEXT_TS", "USER_GENERATED_TS", "END_OF_FRAME",
                                  "NO_FAULT_TOLERANCE", "SYNC", "PWR_CONSTRAINT" };
    s[0] = 0;
    for (int i = 0; i < 12; i++)
        if (f & (1u << i)) { strncat(s, bits[i], n - strlen(s) - 1); strncat(s, "|", n - strlen(s) - 1); }
    if (f & 0x10000) strncat(s, "IFH_NOP|", n - strlen(s) - 1);
    if (f & 0x20000) strncat(s, "SECURE|", n - strlen(s) - 1);
    if (f & 0x40000) strncat(s, "NO_SNAPSHOT|", n - strlen(s) - 1);
    if (f & 0x80000) strncat(s, "SPARSE|", n - strlen(s) - 1);
    char tail[96];
    snprintf(tail, sizeof(tail), "priority=%u preempt_style=%u type=%u other=0x%x",
             (f >> 12) & 0xF, (f >> 25) & 0x7, (f >> 20) & 0x1F, f & ~(0xFFFu | 0xF000 | 0xF0000 | 0x1F00000 | 0xE000000));
    strncat(s, tail, n - strlen(s) - 1);
}

#define MAXT 512
static struct { pid_t tid; int in; uint64_t a0, a1, a2; double t; } th[MAXT];
static long counts[256];
static long cmds_total, ib_bytes_total, submits;
static uint64_t ib_max;

static int slot(pid_t tid)
{
    for (int i = 0; i < MAXT; i++) if (th[i].tid == tid) return i;
    for (int i = 0; i < MAXT; i++) if (!th[i].tid) { th[i].tid = tid; th[i].in = 0; return i; }
    return 0;
}

static void on_entry(pid_t tid, int s, struct user_pt_regs *r)
{
    th[s].a0 = r->regs[0]; th[s].a1 = r->regs[1]; th[s].a2 = r->regs[2]; th[s].t = now() - t0;
    unsigned cmd = (unsigned)r->regs[1];
    if (((cmd >> 8) & 0xff) != 0x09) return;
    unsigned nr = cmd & 0xff;
    counts[nr]++;
    if (nr == 0x4A) {
        struct gpu_command g;
        if (peek(tid, r->regs[2], &g, sizeof(g))) return;
        uint64_t total = 0, mx = 0;
        char sizes[256] = "";
        for (uint32_t i = 0; i < g.numcmds && i < 64; i++) {
            struct command_object o;
            if (peek(tid, g.cmdlist + (uint64_t)i * g.cmdsize, &o, sizeof(o))) break;
            total += o.size;
            if (o.size > mx) mx = o.size;
            if (i < 6) { char b[24]; snprintf(b, sizeof(b), "%s%llu", i ? "," : "", (unsigned long long)o.size); strncat(sizes, b, sizeof(sizes) - strlen(sizes) - 1); }
        }
        submits++; cmds_total += g.numcmds; ib_bytes_total += total; if (mx > ib_max) ib_max = mx;
        if (!quiet)
            fprintf(out, "%9.4f tid %d GPU_COMMAND ctx %u flags 0x%llx cmds %u ib_bytes %llu [%s%s] objs %u syncs %u ts %u\n",
                    th[s].t, tid, g.context_id, (unsigned long long)g.flags, g.numcmds, (unsigned long long)total,
                    sizes, g.numcmds > 6 ? ",..." : "", g.numobjs, g.numsyncs, g.timestamp);
    } else if (nr == 0x32) {
        struct getproperty p;
        if (peek(tid, r->regs[2], &p, sizeof(p))) return;
        unsigned char v[16] = { 0 };
        size_t n = p.sizebytes < 16 ? p.sizebytes : 16;
        peek(tid, p.value, v, n);
        fprintf(out, "%9.4f tid %d SETPROPERTY type 0x%x size %llu value", th[s].t, tid, p.type, (unsigned long long)p.sizebytes);
        for (size_t i = 0; i < n; i++) fprintf(out, " %02x", v[i]);
        fprintf(out, "\n");
    }
}

static void on_exit_(pid_t tid, int s, struct user_pt_regs *r)
{
    unsigned cmd = (unsigned)th[s].a1;
    if (((cmd >> 8) & 0xff) != 0x09) return;
    unsigned nr = cmd & 0xff;
    long ret = (long)r->regs[0];
    if (nr == 0x13) {
        struct drawctxt_create c;
        if (peek(tid, th[s].a2, &c, sizeof(c))) return;
        char f[384];
        ctx_flags(c.flags, f, sizeof(f));
        fprintf(out, "%9.4f tid %d DRAWCTXT_CREATE flags 0x%08x (%s) -> ctx %u, ret %ld\n", th[s].t, tid, c.flags, f, c.drawctxt_id, ret);
    } else if (nr == 0x4A && ret != 0) {
        fprintf(out, "%9.4f tid %d GPU_COMMAND returned %ld (%s)\n", th[s].t, tid, ret, strerror((int)-ret));
    }
}

int main(int argc, char **argv)
{
    int i = 1;
    out = stderr;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) { out = fopen(argv[++i], "w"); if (!out) { perror("log"); return 2; } }
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else break;
    }
    if (i >= argc) { fprintf(stderr, "usage: kgsl-trace [-o LOG] [-q] -- PROGRAM ARGS...\n"); return 2; }
    setvbuf(out, NULL, _IOLBF, 0);

    pid_t child = fork();
    if (child == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        raise(SIGSTOP);
        execvp(argv[i], argv + i);
        perror("execvp");
        _exit(127);
    }
    int st;
    waitpid(child, &st, 0);
    ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK |
           PTRACE_O_TRACEVFORK | PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL);
    t0 = now();
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    int exit_code = 0;
    for (;;) {
        pid_t tid = waitpid(-1, &st, __WALL);
        if (tid < 0) break;
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            int s = slot(tid); th[s].tid = 0;
            if (tid == child) exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
            continue;
        }
        if (!WIFSTOPPED(st)) continue;
        int sig = WSTOPSIG(st), deliver = 0;
        if (sig == (SIGTRAP | 0x80)) {
            struct user_pt_regs r;
            struct iovec io = { &r, sizeof(r) };
            if (ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, &io) == 0) {
                int s = slot(tid);
                if (!th[s].in) { if (r.regs[8] == NR_IOCTL) on_entry(tid, s, &r); th[s].in = 1; }
                else { if (th[s].a1 && r.regs[8] == NR_IOCTL) on_exit_(tid, s, &r); th[s].in = 0; th[s].a1 = 0; }
                if (r.regs[8] != NR_IOCTL && th[s].in) th[s].a1 = 0;
            }
        } else if (sig == SIGTRAP && (st >> 16)) {
            /* clone/fork/vfork event: the new thread starts stopped and is picked up by waitpid */
        } else if (sig == SIGSTOP) {
            /* a new thread's initial stop, or a real SIGSTOP: do not re-deliver */
        } else {
            deliver = sig;
        }
        ptrace(PTRACE_SYSCALL, tid, 0, deliver);
    }
    fprintf(out, "summary: %ld GPU_COMMAND submissions, %ld command buffers, %lld IB bytes (largest %llu)\n",
            submits, cmds_total, (long long)ib_bytes_total, (unsigned long long)ib_max);
    for (int n = 0; n < 256; n++)
        if (counts[n]) {
            const char *nm = ioctl_name(n);
            fprintf(out, "  ioctl 0x%02x %-30s %ld\n", n, nm ? nm : "?", counts[n]);
        }
    return exit_code;
}
