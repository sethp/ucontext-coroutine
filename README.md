# ucontext-coroutine

An attepmt to implement [coroutines](https://en.wikipedia.org/wiki/Coroutine) via [`ucontext`](https://www.gnu.org/software/libc/manual/html_node/System-V-contexts.html)s.

Note: this README is presented out-of-order relative to how I learned my way through in an attempt to make the example more broadly useful, but it presents both the problem and solution fait accompli. If you'd prefer to see a less polished but more realistic record of the journey, please see [NOTES.md](NOTES.md).

## What

Problem statement:

> Implement a M:N statically-scheduled stackful coroutine model for cooperative multi-tasking.

Unpacking that a bit:

* M:N (scheduling): Running M concurrent user tasks across N parallel executors (i.e. OS threads).
* Statically-scheduled: Each task and its mapping to the underlying executor is defined at build time.  
* Stackful coroutine: a stackful coroutine supports pausing & resuming a user task from arbitrary call depths. This is in contrast to a _stackless_ coroutine, which is much simpler to reason about and implement because it can only suspend/resume from the top level, but can't express e.g. recursion. Compare: Go's goroutines (stackful) vs. Javascript's `setTimeout` (stackless). 
* Cooperative multi-tasking: our tasks must explicitly yield to each other, there's no out-of-band process that will pre-empt a running piece of work. In other words, every context switch is _explicit_, and every task must have some idea of who to switch _to_.

## Building & Running the Example

```
make && ./coroutine
```

ought to produce output something like:

```
cc coroutine.c -o coroutine
consumed 0 (total: 0)
producing 4....consumed 4 (total: 4)
producing 4....consumed 4 (total: 8)
producing 4....consumed 4 (total: 12)
producing 4....consumed 4 (total: 16)
producing 4....consumed 4 (total: 20)
producing 4....consumed 4 (total: 24)
producing 4....consumed 4 (total: 28)
producing 4....consumed 4 (total: 32)
All done, exiting!
```

That output indicates successful multiplexing of our m tasks across n threads (for n=1). 

Suggestions:

* Try tracing the control flow through `swapcontext` calls, and across the state set by `makecontext`. How would you identify all possible paths that lead to execution continuing after a `swapcontext` call?
* Why do you think `getcontext` is required before a call to `makecontext`?
* What distinguishes a `getcontext`/`setcontext` pair from `makecontext`/`swapcontext`?
* How is `getcontext`/`setcontext` different from `sigsetjmp`/`siglongjmp`? 