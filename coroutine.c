
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

static int switches = 4;

// represents work in flight
static int in_flight;

static const int batch_size = 4;
static int total = 0;

static void producer(ucontext_t *self, ucontext_t *target)
{
    while (1)
    {
        printf("producing %d", batch_size);
        fflush(stdout);
        in_flight += batch_size;
        swapcontext(self, target);
    }
}

static void consumer(ucontext_t *self, ucontext_t *target)
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
            swapcontext(self, target);
            last = in_flight;
        }
    }
}

int main(void)
{
    const int m = 2;
    ucontext_t uc[m + 1];
    char st[m][8192];

    // set up tasks
    for (int i = 0; i < m; i++)
    {
        if (getcontext(&uc[i + 1]) == -1)
            abort();

        uc[i + 1].uc_link = &uc[0]; // main context
        uc[i + 1].uc_stack.ss_sp = st[i];
        uc[i + 1].uc_stack.ss_size = sizeof st[i];
    }

    // "scheduler"
    const int argc = 2 * sizeof(ucontext_t *) / sizeof(int);
    makecontext(&uc[1], (void (*)(void))producer, argc, &uc[1], &uc[2]);
    makecontext(&uc[2], (void (*)(void))consumer, argc, &uc[2], &uc[1]);

    // go
    swapcontext(&uc[0], &uc[2]);

    // we'll get here after either:
    // - `swapcontext(..., &uc[0])`
    // - either of the `uc_link`'d functions returns
    printf("All done, exiting!\n");

    return 0;
}
