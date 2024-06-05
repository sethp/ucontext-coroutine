# Context

This is more of an append-mostly log of the notes I took as I went (vs. README.md). I'm intending to avoid going back and re-editing earlier sections too much as my understanding improves, which may make this journal more useful to you if you've got the same questions & misunderstandings I do. In the event that you're coming to this from a different place than where I happened to be (which seems quite likely, since I haven't noticed you hovering over my shoulder for the last 15 years), it's probably going to read more like a tedious stream-of-conciousness in dire need of some editing. Either way, I'd be grateful to hear your feedback.

## Why

The motivating example for me is trying to understand a crash in a [verilated] model of [a NIC implementation][icenic] which relies on the RISC-V [fesvr] ("front-end server") C++ [`context_t`][context_t] ucontext wrapper for [work handling](https://github.com/firesim/icenet/blob/7c17985a46108c8ff8d424b1b3af6eebc37c38a3/src/main/resources/csrc/SimNetwork.cc#L54-L59) and only occurs when [multithreading](https://verilator.org/guide/latest/verilating.html#multithreading). Conveniently, the crash is both immediate and deterministic for a given verilated model, which makes it an excellent learning opportunity.

The full description of the crash is here: https://github.com/ucb-bar/chipyard/issues/1885

[verilated]: https://verilator.org/guide/latest/overview.html#overview
[icenic]: https://github.com/firesim/icenet/blob/7c17985a46108c8ff8d424b1b3af6eebc37c38a3/src/main/resources/csrc/SimNetwork.cc
[fesvr]: https://github.com/riscv-software-src/riscv-isa-sim/blob/master/fesvr
[context_t]: (https://github.com/riscv-software-src/riscv-isa-sim/blob/2cfd5393520c0673fd0182fcca430351d9e6682d/fesvr/context.h)

If you, like me, want some help unpacking that _whole deal_, then this worked example is for you.

Specifically, I'm hoping to answer these questions for myself:

* Why does the fesvr's `context_t` use a thread-local? What is it tracking in there?
* Which implementation choices exist in the problem domain? Which does the `fesvr` express?
* Does the fesvr wrapper make sense in a multi-threaded context, or not?

### No, but why ucontext

Why add more ucontext code to the world in 2024, when it's been long-deprecated & was [removed from POSIX](https://stackoverflow.com/a/5541587) in 2008?

Well, besides there being a lot of ucontext code still in the world (over ~20k references [just on github](https://github.com/search?q=%22%3Cucontext.h%3E%22&type=code)), it's still pretty well-supported by lots of operating systems on lots of architectures. Despite being "obsolescent," there's not many alternatives: Boost's [docs](https://www.boost.org/doc/libs/1_85_0/libs/coroutine2/doc/html/coroutine2/coroutine/implementations__fcontext_t__ucontext_t_and_winfiber.html) suggest to me that besides windows-specific WinFiber and writing it yourself (e.g. boost) there's not much in the way of other options. And if you want split stacks (which you probably do for a language runtime that's expecting a _lot_ of contexts), well, apparently that's a ucontext-only deal in Boost-land.

The main drawback appears to be the performance cost of saving/restoring the signal mask: https://stackoverflow.com/questions/33331894/why-does-ucontext-have-such-high-overhead, which may or may not be super relevant in our `fesvr`/verilated RTL model situation. Boost's `fcontext_t` mainly seems to differ in this one choice, so that's something to potentially come back to. There's also an API-compatibile implementation in https://github.com/kaniini/libucontext that offers a build-time config flag for the signal-mask behavior, so that's another interesting reference point.

Plus, it's kind of my _vibe_; have I always wanted to say "I implemented my own coroutines on top of a deprecated 20-year-old glibc primitive?" No, I haven't. Have I wanted to since I discovered that was a thing I _might_ be able to say? Absolutely, yes. There's a half dozen of us, at least: https://stackoverflow.com/questions/765368/how-to-implement-a-practical-fiber-scheduler

### Stackful vs. Stackless?

So, the fesvr uses the stackful thing heavily, and it's familiar from writing Go. But, also, "any function you call may spawn tasks/suspend excution at any time" doesn't compose: https://vorpus.org/blog/notes-on-structured-concurrency-or-go-statement-considered-harmful/#go-statement-considered-harmful. On the other hand, stackful coroutines can express concurrent recursion, so it's [impossible to say whether it's bad or not](https://twitter.com/dril/status/464802196060917762).

It's the difference between:

```go
for {
    conn, _ := socket.Accept()

    go handle(conn)
}
```

and

```python
readable, _, _, = socket.select([conn], [], [], 0) # or poll/epoll

return [(handle_fn, conn.accept()) for conn in readable]
```

(with the idea that the caller will schedule the handle_fn to be called with the rest of the tuple as arguments).


# Process

I started with getting the coroutine example from https://www.gnu.org/software/libc/manual/html_node/System-V-contexts.html running: that was as straightforward as copying the example into `coroutine.c` and running `cc coroutine.c -o coroutine && ./coroutine` from my shell. Always nice when the documentation works on the first try.

That printed quite a lot of _stuff_ to my terminal, mostly walls of `.` interspersed with `switching from 1 to 2` or `switching from 2 to 1`. So I fiddled with some of the constants as a way to test my understanding; changing:

```diff
-if (++switches == 20)
+if (++switches == 4)
     return;
```

worked as I intended (fewer switches before exit), but trying to print fewer dots before switch I got backwards.

```diff
-if (++m % 100 == 0)
+if (++m % 5 == 0) // oops: prints 20x as often, not 1/20th as often
 {
     putchar ('.');
     fflush (stdout);
 }
```

That's OK, though, I got there eventually:

```diff
 /* This is where the work would be done. */
+putchar('.');
 if (++m % 5 == 0)
 {
-    putchar ('.');
     fflush (stdout);
+    printf("\nswitching from %d to %d\n", n, 3 - n);
+    swapcontext(&uc[n], &uc[3 - n]);
 }

 /* Regularly the expire variable must be checked. */
 if (expired)
 {
     /* We do not want the program to run forever. */
     if (++switches == 4)
        return;

-    printf ("\nswitching from %d to %d\n", n, 3 - n);
     expired = 0;
-    /* Switch to the other context, saving the current one. */
-    swapcontext (&uc[n], &uc[3 - n]);
 }
```

both allowed removing the timer signal entirely and produced a much more managable output:

```
.....
switching from 1 to 2
.....
switching from 2 to 1
.....
switching from 1 to 2
.....
switching from 2 to 1

All done, exiting!
```

I got curious about what that blank line was doing there, which turned into some fiddling around with the newline character that ended up being an accidentally effective way to understand the control flow and ordering around caller/callee.

## reading the (fine) manual

At around this point, too, I read my way through the glibc documentation; it's well written and I won't ruin it for you, but I did find futzing with the example a very helpful pre-read. That primed me to understand, for example:

* `makecontext` and `getcontext` are kind of... backwards? Or, at least `getcontext` has to be called _first_ (before `makecontext`) to initialize the ucontext_t
* it's OK to point to an un-initialized `ucontext_t`, as long as you initialize it (with `swapcontext`) before any of the pointees try to make use of it.
    * this establishes a happens-before relationship that's potentially spicy with multiple threads; maybe that's partially what they mean by `MT-Safe race:ucp`?
    * trying to figure out the threading is gonna be a whole thing, isn't it
* `setcontext` is more like `goto`; there's no state saved before the outbound edge, and so no way back to the call site.
    * I guess this is kind of related to the `setjmp`/`longjmp` idea where "throwing an exception" means "replace the currently executing context entirely, don't worry about resuming it"; it's not so useful for our purposes though
* `swapcontext` is maybe more similar to a `call`: but instead of saving the caller address to the stack—which is being replaced—it saves it off to the region pointed to by the `oucp` (original? user context pointer) parameter.
* Passing function arguments is also a bit strange: they're "side-loaded" ahead of the `swapcontext`/`setcontext` call by setting some structure fields and then calling `makecontext` (see questions below); `makecontext` takes a variable number of arguments, but in kind of an unusual way: it takes an `argc` followed by `argc` "integers". But also, if we get the argc number right, it seems perfectly fine with pointers (which are `long long` on my 64-bit platform), so..... ABIs, amirite?
    * me *through tears*: you can't just pretend everything is an integer
    * c compiler: *points at...... anything* integer


*Question*: what happens if the stack parameters are set up in the structure but `makecontext` isn't called? Or if they're changed after the `makecontext` call?
    - i.e. is there any order dependence here? YES (see below).

*Question*: Can we pass a stack parameter by pre-populating the lower addresses of the stack, and then setting the `makecontext` call up to "point" higher up / with a parameter of the "lower" addresses?
    - sure seems like it; and, in fact, that's almost certainly what `makecontext` is doing under the hood
    - see also: https://github.com/kaniini/libucontext/blob/be80075e957c4a61a6415c280802fea9001201a2/arch/riscv64/makecontext.c#L52-L54

*Question*: what happens if passing a ucp initialized by `getcontext` in one thread to a `swapcontext` in a different thread?
    - are "thread locals" (well, the thread-local base) part of the context that gets saved/restored?
    - NO (not in libucontext, at least): https://github.com/kaniini/libucontext/blob/be80075e957c4a61a6415c280802fea9001201a2/arch/riscv64/getcontext.S#L18


## (getting to) setting up the m tasks

So, changing `main` around to use a loop for setup seemed like a good idea; in order to answer the first question above though we need a separate `getcontext` pre-init loop from setting up the structure fields (calling `makecontext` prior to `getcontext` just seems to make the latter hang; attempting to skip `getcontext` enirely segfaults).

### order dependence of `makecontext`

So the structure bits seem to be getting memoized in the `makecontext` call; modifying those after calling `makecontext` doesn't seem to have any effect. Deferring the setup like so:

```c
    // set up tasks
    for (int i = 0; i < n; i++)
    {
        if (getcontext(&uc[i + 1]) == -1)
            abort();

        uc[i + 1].uc_stack.ss_size = sizeof st[i];
        uc[i + 1].uc_stack.ss_sp = st[i];
    }

    // "scheduler"
    const int argc = 2 * sizeof(ucontext_t *) / sizeof(int);
    makecontext(&uc[1], (void (*)(void))producer, argc, &uc[1], &uc[2]);
    makecontext(&uc[2], (void (*)(void))consumer, argc, &uc[2], &uc[1]);

    for (int i = 0; i < n; i++)
    {
        uc[i + 1].uc_link = &uc[0]; // main context
    }
````

works fine, just until it comes time to return. Then, we segfault in setcontext approximately when we're trying to return (i.e. when that uc_link would actally be used). We can learn about what triggered the crash:

```
$ coredumpctl debug
...

(gdb) display/i $pc
1: x/i $pc
=> 0x7f1bb1fd4514 <setcontext+52>:	fldenv (%rcx)
(gdb) p $rcx
$1 = 1
```

`1` seems a little odd (it's a little.... nice, to be garbage), but it's probably not a valid memory address, so yeah that's a crash.

Trying to reproduce that failure by setting `uc_link` to an invalid value is a _little_ tricky; e.g.

```diff
 // set up tasks
 for (int i = 0; i < n; i++)
 {
     if (getcontext(&uc[i + 1]) == -1)
         abort();

-    uc[i + 1].uc_link = &uc[0];
+    printf("uc_link: %llx\n", uc[i + 1].uc_link);
+    uc[i + 1].uc_link = (ucontext_t *)1;
     uc[i + 1].uc_stack.ss_size = sizeof st[i];
     uc[i + 1].uc_stack.ss_sp = st[i];
 }
```

prints out something like:

```
uc_link: 1
uc_link: 7ffe6c454a90
...
```

before exiting 255 (i.e. not a segfault). Still, there's at least two kinds of UB going on in that program, so it's probably something that's not really worth chasing down.

Suffice to say, yes: there's a strong ordering dependence on setting up those parameters and the `makecontext` call.


## towards n threads

The struggle at this point seems to be crafting an example that:

1) triggers "interesting" swaps (i.e. suspend on one thread, resume on another)
2) is simple enough to reason about that it doesn't distract from the task at hand

Maybe some sort of batched scatter/gather thing? Like, a bunch of independent tasks are all bumping their local counters, and there's a "gather" that runs through and accumulates them all into a single sum? And when it "runs out", it goes and asks for more (-> kicks off the multiple producers again)?

Yeah, that seems like it could work; that would let us do:

1 main task that spawns M tasks & N threads, and "kicks things off" by triggering the gather task.

1 gather task that just iterates some fixed-size array and accumulates; exiting when we exceed some accumulated total again.

N scatter tasks (to saturate our threads) that each gets a "slot" in the array to increment.

challenges:

* how does the gather task kick off all the scatter tasks (fan out)? We need something like a latch/barrier that parks the underlying pthreads until they're told to go.
* how do the fan-in from finishing the producer work? another latch means we'd only have N-1 threads available for gathering
    * hmm, we also probably want a constriction point here to linearize printing, too
* how do we do scheduling? We want to ensure:
    * each execution is _deterministic_
    * at least one time we hop across thread boundaries between a `makecontext` and a `swapcontext`
    * each thread gets at least some work

### but first, a detour

it feels like it'd be nicer to be able to say `yield_to(fn)` rather than passing around a bunch of pointers to contexts and trying to track the mapping between pointers and functions by hand. Also, it feels a little sketchy to me to be doing this:


```c
static void consumer(ucontext_t *self, ucontext_t *target);
// ...

const int argc = 2 * sizeof(ucontext_t *) / sizeof(int);
makecontext(&uc[1], (void (*)(void))producer, argc, &uc[1], &uc[2]);
```

like, it clearly _works_, but `int` is 1/2 the size of the pointer? so `makecontext` ought to be setting up the call with 4 arguments, but we're only giving it 2? which is ok, I guess, because each of those 2 is 2x the width it should be, but...

Like, it's probably working because `makecontext` and `consumer` happen to share a similar-enough ABI that however the pointers are passed to `makecontext` (i.e. register promotion, stack spilling, whatever) matches how the compiler expects to read them when they're passed to `consumer`. But there's usually a "discontinuity" in ABIs at some argument count (e.g. the "common" RISC-V ABI expects to see the first 8 arguments in registers, then the rest spill into the stack); it seems like it'd be quite possible to pass "too many" arguments to `makecontext` and trigger a difference between how they're `va_arg`s'd...

This isn't just a theoretical concern, either; the fesvr's `context.h` has this snippet:

```cpp
#ifndef GLIBC_64BIT_PTR_BUG
  static void wrapper(context_t*);
#else
  static void wrapper(unsigned int, unsigned int);
#endif
```
—https://github.com/riscv-software-src/riscv-isa-sim/blob/2cfd5393520c0673fd0182fcca430351d9e6682d/fesvr/context.h#L40-L44

Which sure looks to me like they ran into at least one situation where it _didn't_ "just work." Tracing that back across repos, it looks like that code came from: https://github.com/riscvarchive/riscv-fesvr/pull/15 . Apparently, there's Special Code on Linux in glibc for passing 64-bit pointers this way. So probably e.g. libucontext _wouldn't_ work, nor would picking a different operating system.

On Linux with glibc, though, apparently the Special Code has been part of glibc mainline since version 2.8, which is from ~2008 or so: https://sourceware.org/glibc/wiki/Glibc%20Timeline . So, yeah, that's code that's legally old enough to seek emancipation in any state in the United States, and old enough to legally drink in some parts of Europe. Yikes.

Ok, back onto our main detour from the detour's detour through ways to contextualize how old I'm feeling right now. The thing that'd feel an awful lot clearer and safer would be to implement something like:

```c
void yield_to(void (*f)(void));
```

Which we'd have to be careful to set up ahead of time, since we can't just _call_ a function with `makecontext`/`swapcontext`. I mean, we could, but we want the function to name a _task_ which means calling it "from the top" would be some boring stackless coroutine shit and we're not here for that.

Instead, let's make a table for "task identifier" (right now, function; that's gonna get spicy in a minute tho) to "task context" (i.e. `ucontext_t`):

```c
static struct
{
    void (*f)(void);
    ucontext_t uc;
} contexts[] = {
   // ...
};
```

then, in `yield_to`, we can scan that table to find the context matching the identifier (function pointer) and invoke it.

ah, but a wrinkle:

```c
static void yield_to(void (*f)(void))
{
    const int NN = sizeof(contexts) / sizeof(contexts[0]);
    for (int i = 0; i < NN; i++)
        if (contexts[i].f == f)
        {
            swapcontext(&contexts[1 - i].uc, &contexts[i].uc);
            return;
        }
}
```

This works, but only as long as there are exactly 2 user tasks. The trouble is the `oucp` argument (the first, to `swapcontext`): we know what context we want to invoke, but we also have to save our current context in a place where it will be found again.

In other words, consider:

1. in taskA: yield_to(taskB)
2. in taskB: yield_to(taskA)

It's fairly clear that we'd like taskA to resume from the `yield_to` call, which means we need to `swapcontext(..., &storage_from_yield_to_taskB)`.

Ah, so that's what the other parameter to `swapcontext` is for: it's not enough to invoke the name of the target, you also need to know what other name you'll be called _by_.

So it's not quite right to say:

`yield_to(self, other)`

It's more like:

`yield_to(other's name for me, other)`

What happens if we have multiple call-return sites?

    i.e. (diamond-like pattern)
    ```
    task T:
        if i % 2 == 0:
            yield L
        else:
            yield R

    task L:
        yield B

    task R:
        yield B

    task B:
        yield T
    ```

    yeah, that works; it _wouldn't_ work to indirect through pointers that changed (i.e. we can't do local renaming)

Perhaps our static scheduling/task definition can save us here?

It's also worth noticing: https://github.com/riscv-software-src/riscv-isa-sim/blob/a53a71fcc3c985cf95973e86e40814c30c551a68/fesvr/context.cc#L81

takes two arguments (both implicit): a thread-local and the `this` pointer.

libucontext's example https://github.com/kaniini/libucontext/blob/be80075e957c4a61a6415c280802fea9001201a2/examples/cooperative_threading.c#L15 also takes two parameters, one implicitly: that's the "last yielded from" param.

It seems like there's an implicit "stack" here that we're subtly ducking by only having two tasks (so there's no possibility of needing to "yield" from three deep).

Perhaps it would be enough to try:

```diff
 static void yield()
 {
-    ucontext_t *next = prev
+    ucontext_t *next = prev, *old_prev = prev;
     prev = cur;
     swapcontext(prev, cur = next);

+    cur = prev;
+    prev = old_prev;
 }
```

That said, in the single producer/consumer pair model we could get away with this:

```diff
     // go
-    swapcontext(prev = &uc_main, cur = &contexts[0].uc);
+    prev = &contexts[1].uc;
+    swapcontext(&uc_main, cur = &contexts[0].uc);
```

and having both `producer` and `consumer` simply `yield()` to each other.

### having created that model, we must immediately break it

the problem with using functions to name work is that

1. it only makes sense once, on entry, and after that it's confusing
2. it means we can't make copies of a single function

so, instead, it's time for a `task_t`; statically, we still get the possibility of naming tasks as variables, but with a slightly bettter "handle."

For now, it seems all we need to do is just wrap the `ucontext`:

```c
typedef struct
{
    ucontext_t uc;
} task_t;
```

But, soon, we'll make use of that to store some task-local state.

