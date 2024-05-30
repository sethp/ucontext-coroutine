
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

static void producer()
{
    while (1)
    {
        printf("producing %d", batch_size);
        fflush(stdout);
        in_flight += batch_size;
        yield();
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
    swapcontext(prev = &uc_main, cur = &contexts[0].uc);

    // we'll get here after either:
    // - `swapcontext(..., &uc[0])`
    // - either of the `uc_link`'d functions returns
    printf("All done, exiting!\n");

    return 0;
}
