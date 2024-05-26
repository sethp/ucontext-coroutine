
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

// represents work in flight
static int in_flight;

static const int batch_size = 4;
static int total = 0;

static void producer();
static void consumer();

static struct
{
    void (*f)(void);
    ucontext_t uc;
} contexts[] = {
    {.f = consumer},
    {.f = producer},
};

static void yield_to(void (*f)(void))
{
    const int NN = sizeof(contexts) / sizeof(contexts[0]);
    for (int i = 0; i < NN; i++)
        if (contexts[i].f == f)
        {
            // interesting; trying to suspend/restore like this _almost_ works:
            // static __thread ucontext_t cur;
            // swapcontext(&cur, &contexts[i].uc);
            // but, `last` is always 0? i.e., it seems like we're losing track of the
            // caller's stack.

            // this works, because there's exactly 2.
            // in general, how can we identify the caller's context?
            swapcontext(&contexts[1 - i].uc, &contexts[i].uc);
            return;
        }
}

static void producer()
{
    while (1)
    {
        printf("producing %d", batch_size);
        fflush(stdout);
        in_flight += batch_size;
        yield_to(consumer);
    }
}

static void consumer()
{
    int last;
    while (1)
    {
        if (in_flight > 0)
        {
            putchar('.');
            in_flight--;
            total++;
        }
        else
        {
            printf("consumed %d (total: %d)\n", last, total);
            if (total >= 32)
                return;
            yield_to(producer);
            last = in_flight;
        }
    }
}

int main(void)
{
    const int m = sizeof(contexts) / sizeof(contexts[0]);
    struct ucontext_t uc_main;
    char st[m][8192];

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
    swapcontext(&uc_main, &contexts[0].uc);

    // we'll get here after either:
    // - `swapcontext(..., &uc[0])`
    // - either of the `uc_link`'d functions returns
    printf("All done, exiting!\n");

    return 0;
}
