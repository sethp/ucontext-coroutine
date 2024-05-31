
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define N 2

// represents work in flight
static volatile int in_flight[N];

static const int batch_size = 4;
static int total = 0;

static void gather();
static void scatter_all();
static void scatter(int n);

static struct
{
    void (*f)(void);
    ucontext_t uc;
} contexts[] = {
    {.f = gather},
    // {.f = scatter},
    {.f = scatter_all},
};

ucontext_t uc_main;
ucontext_t *cur = &uc_main;
ucontext_t *prev = NULL;

static void yield_to(void (*f)(void))
{
    const int NN = sizeof(contexts) / sizeof(contexts[0]);

    prev = cur;
    for (int i = 0; i < NN; i++)
        if (contexts[i].f == f)
        {
            ucontext_t *next = &contexts[i].uc;
            swapcontext(prev, cur = next);
            return;
        }
}

static void yield()
{
    ucontext_t *next = prev;
    prev = cur;
    swapcontext(prev, cur = next);
}

static void scatter_all()
{
    ucontext_t uc[N];
    char st[N][4096]; // doesn't want to be < 4096 (yikes)

    for (int i = 0; i < N; i++)
    {
        ucontext_t *ucp = &uc[i];
        if (getcontext(ucp) == -1)
            abort();

        ucp->uc_link = cur; // should maybe be an errexit of some kind (?)
        ucp->uc_stack.ss_sp = st[i];
        ucp->uc_stack.ss_size = sizeof st[i];

        makecontext(ucp, (void (*)(void))scatter, 1, i);
    }

    ucontext_t *scatter_uc = cur;

    printf("scatter_all: init done\n");
    while (1)
    {
        for (int i = 0; i < N; i++)
        {
            prev = scatter_uc;
            swapcontext(prev, cur = &uc[i]);
        }

        yield_to(gather);
    }
}

static void scatter(int n)
{
    while (1)
    {
        printf("(id=%d) producing %d\n", n, batch_size);
        fflush(stdout);
        in_flight[n] += batch_size;
        yield();
    }
}

static void gather()
{
    while (1)
    {
        int got = 0;
        for (int i = 0; i < N; i++)
        {
            // CONSUME
            got += in_flight[i];
            in_flight[i] = 0;
        }

        total += got;
        printf("accumulating... got %d (total: %d)\n", got, total);
        if (total >= 32 * N)
            return;
        yield_to(scatter_all);
    }
}

int main(void)
{
    const int m = sizeof(contexts) / sizeof(contexts[0]);

    // TODO[seth]: oops, we ran out of stack space, and all we got was a segfault.
    char st[m][8192 * (N + 1)];

    // set up tasks
    for (int i = 0; i < m; i++)
    {
        ucontext_t *ucp = &contexts[i].uc;
        if (getcontext(ucp) == -1)
            abort();

        ucp->uc_link = &uc_main; // main context
        ucp->uc_stack.ss_sp = st[i];
        ucp->uc_stack.ss_size = sizeof st[i];

        makecontext(ucp, contexts[i].f, 0);
    }

    // go
    swapcontext(prev = &uc_main, cur = &contexts[0].uc);

    // we'll get here after either:
    // - `swapcontext(..., &uc[0])`
    // - either of the `uc_link`'d functions returns
    printf("All done, exiting!\n");

    return 0;
}
