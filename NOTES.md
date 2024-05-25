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


