
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

// We have N "scatter" tasks that each produce units of work,
// and a single "gather" task that processes those work items.
#define N 2
static volatile int in_flight[N]; //< represents work in flight
static const int batch_size = 8;  //< how many units are produced per task
static int total = 0;             //< an accumulator that tracks processed work

typedef struct
{
    ucontext_t uc;
} task_t;

// These are the task entry points
static void gather();
static void scatter_all();
static void scatter(int n);

static task_t gather_task;
static task_t scatter_all_task;

task_t main_task;

ucontext_t *cur = &main_task.uc;
ucontext_t *prev = NULL;

static void yield_to(task_t *task)
{
    prev = cur;
    swapcontext(prev, cur = &task->uc);
}

static void yield()
{
    ucontext_t *next = prev;
    prev = cur;
    swapcontext(prev, cur = next);
}

static void scatter_all()
{
    task_t scatter_tasks[N];
    char st[N][4096]; // doesn't want to be < 4096 (yikes)

    for (int i = 0; i < N; i++)
    {
        ucontext_t *ucp = &scatter_tasks[i].uc;
        if (getcontext(ucp) == -1)
            abort();

        ucp->uc_link = cur; // should maybe be an errexit of some kind (?)
        ucp->uc_stack.ss_sp = st[i];
        ucp->uc_stack.ss_size = sizeof st[i];

        makecontext(ucp, (void (*)(void))scatter, 1, i);
    }

    printf("scatter_all: init done\n");
    while (1)
    {
        for (int i = 0; i < N; i++)
        {
            yield_to(&scatter_tasks[i]);
        }

        yield_to(&gather_task);
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
        yield_to(&scatter_all_task);
    }
}

int main(void)
{
    const int m = 2 + N;

    // TODO[seth]: oops, we ran out of stack space, and all we got was a segfault.
    char st[m][8192 * (N + 1)]; // allocate heterogeneous stacks?

    struct
    {
        task_t *t;
        void (*f)(void);
    } tasks[] = {
        {
            .t = &gather_task,
            .f = gather,
        },
        {
            .t = &scatter_all_task,
            .f = scatter_all,
        },
    };

    const int NT = sizeof(tasks) / sizeof(tasks[0]);
    // set up tasks
    for (int i = 0; i < NT; i++)
    {
        ucontext_t *ucp = &(tasks[i].t->uc);
        if (getcontext(ucp) == -1)
            abort();

        ucp->uc_link = &main_task.uc; // main context
        ucp->uc_stack.ss_sp = st[i];
        ucp->uc_stack.ss_size = sizeof st[i];

        makecontext(ucp, tasks[i].f, 0);
    }

    // go
    // swapcontext(prev = &main_task.uc, cur = &gather_task.uc);
    yield_to(&gather_task);

    // we'll get here after either:
    // - `swapcontext(..., &main_task.uc)`
    // - either of the `uc_link`'d functions returns
    printf("All done, exiting!\n");

    return 0;
}
