/*
 * UTDeck probe: give an Android (bionic) Turnip loaded through libhybris glibc's
 * pthread_barrier_* functions, in any program, by LD_PRELOAD.
 *
 * libhybris routes an Android library's pthread calls to glibc through its hook table. That
 * table has no pthread_barrier_*, which Mesa's util_queue_finish() calls in every
 * vkDestroyDevice: without this, Turnip crashes with SIGBUS at a fake address
 * (dolphin-ut stage 4, S4-M1). Dolphin installs the same hook from its own code
 * (Source/Core/DolphinUT/VulkanDriver.cpp); this library does it for programs we cannot change,
 * such as Geekbench.
 *
 * The types match on arm64: pthread_barrier_t is 32 bytes in both C libraries, and both return
 * -1 (PTHREAD_BARRIER_SERIAL_THREAD) to one waiter.
 *
 * Build (arm64 glibc): aarch64-linux-gnu-gcc -O2 -shared -fPIC -o hybris-barrier-hook.so \
 *                      hybris-barrier-hook.c -ldl -lpthread
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(pthread_barrier_t) == 32, "barrier size differs from bionic's");

static void *hook_symbol(const char *name, const char *requester)
{
    (void)requester;
    if (strncmp(name, "pthread_barrier_", 16) != 0)
        return NULL;
    if (strcmp(name + 16, "init") == 0)
        return (void *)&pthread_barrier_init;
    if (strcmp(name + 16, "wait") == 0)
        return (void *)&pthread_barrier_wait;
    if (strcmp(name + 16, "destroy") == 0)
        return (void *)&pthread_barrier_destroy;
    return NULL;
}

__attribute__((constructor)) static void install_hook(void)
{
    /* The same libhybris instance the program's EGL and Vulkan libraries use later. */
    void *hybris = dlopen("libhybris-common.so.1", RTLD_NOW | RTLD_GLOBAL);
    void (*set_callback)(void *(*)(const char *, const char *)) =
        hybris ? (void (*)(void *(*)(const char *, const char *)))dlsym(hybris, "hybris_set_hook_callback")
               : NULL;
    if (!set_callback) {
        fprintf(stderr, "hybris-barrier-hook: libhybris has no hook callback (%s)\n",
                hybris ? "symbol missing" : dlerror());
        return;
    }
    set_callback(hook_symbol);
    fprintf(stderr, "hybris-barrier-hook: pthread_barrier_* hooked\n");
}
