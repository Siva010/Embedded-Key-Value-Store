# AI Development Workflow Rules

## Git Workflow (Mandatory)

Treat this project like a professional production codebase.

After EVERY meaningful task:

-   Create a local Git commit.
-   Never wait until an entire phase is complete.
-   Never push automatically.

Examples of commit boundaries:

-   Project scaffold
-   CMake setup
-   Public API
-   Hash index
-   Append-only log
-   Serialization
-   CRC32
-   Recovery
-   Compaction
-   Concurrency
-   Tests
-   Benchmarks
-   Documentation

Use Conventional Commits.

Examples:

``` text
feat: initialize project

build: configure CMake

feat(store): implement KV API

feat(storage): implement append-only log

feat(recovery): add startup replay

feat(compaction): implement log compaction

feat(concurrency): add shared mutex

test(storage): add persistence tests

bench: add throughput benchmark

docs: document architecture

fix(recovery): prevent replay of corrupted tail record
```

Never execute:

``` bash
git push
```

unless explicitly instructed.

After each task report:

-   Files changed
-   Why the commit was made
-   Suggested commit message

------------------------------------------------------------------------

# Implementation Discipline

Implement ONE task at a time.

Do not start the next task until the current one is:

1.  Implemented
2.  Compiled
3.  Tested
4.  Documented
5.  Committed

After every task:

-   Explain implementation
-   Explain tradeoffs
-   Build/run if possible
-   Commit locally
-   Stop and wait for the next instruction

Prioritize maintainability, readability, modularity, and reviewable
commits over implementation speed.
