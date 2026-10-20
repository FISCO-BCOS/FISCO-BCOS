## Summary

Replace the legacy boost::log **core** pipeline with a whole-line fast path, keeping the boost **backends** (rotation, file collector, auto-flush):

- **`LogStream`** (new): boost-free `std::ostream` facade with a 512 B inline arena, so typical lines are formatted without a heap allocation. `BCOS_LOG(level)` expands to `if (level >= c_fileLogLevel) bcos::LogStream(level)`.
- **Producers format the complete line** (`severity|timestamp|thread|message`) into a pooled `std::string` slot and hand the slot over the pool; the feeding thread passes the line straight to the backend. boost::log record construction and the per-record global attribute set are gone from the hot path.
- **`LineAsyncSink`** (new): fixed slot pool with drop-on-overflow instead of blocking the producer, and a `locked_backend()` that reproduces `asynchronous_sink`'s configuration interface, so `BoostLogInitializer`'s setup code is nearly unchanged.
- **`FATAL` is fail-stop by construction**: `BCOS_LOG(FATAL)` bypasses the queue and is written synchronously on the producer thread, and both backends flush before `std::abort()` — a FATAL can neither be dropped on overflow nor lost in the filebuf.
- **Timely durability without a per-line flush**: the feeding thread flushes whenever the queue drains, and at least every `LineAsyncSink::FlushInterval` (100 ms) under a sustained flood. `log.flush` keeps its `false` default; `flush=true` restores per-line auto-flush. Documented in the shipped `config.ini.*` templates.
- **Overflow is observable**: dropped lines are counted and reported once the backlog clears with `[log] dropped N log line(s): slot pool exhausted`.
- **Pool memory is bounded**: slots keep their heap capacity for reuse, but a slot that held a line beyond 4 KB gets a fresh string when it returns to the pool.
- Prune per-record attributes (LineID/ProcessID only with custom `log.format`) and throttle time-based rotation predicates to 1 s granularity.
- `BoostLog.h` is reduced to a minimal shim; `LogStream`'s destructor commits directly to the registered line sinks.
- Delete the stat log channel (`StatFileLoggerHandler`, `setStatLogLevel`, `c_statLogLevel`) and `FileLoggerHandler`. A non-empty `log.format` now emits a `BCOS_LOG(WARNING)` and is ignored.
- Portability: the sink registry uses a `shared_mutex`-guarded slot array instead of `std::atomic<std::shared_ptr>` (not portable across toolchains); `pthread`/`clock_gettime`/`localtime_r` are guarded for Windows (`GetCurrentThreadId`, `std::chrono`, `localtime_s`), with `ERROR` re-`#undef`-ed after `<windows.h>`.

## Performance

Enabled-path caller-side latency: **~1900ns -> ~580ns** per call (spdlog async: ~400ns); disabled-level path: **~0.3ns** (spdlog: ~2.2ns).

## Compatibility

- Default line layout is unchanged — byte-identical to the legacy pipeline for the built-in format, including the `Unnamed-0x<tid>` thread field for threads without a name.
- Custom `log.format` is no longer supported: the key is read, a `WARNING` is logged, and the built-in layout is used.
- `log.flush` default changes from `true` to `false`; per-line durability is replaced by flush-on-drain, bounded by `FlushInterval`. Set `flush=true` for the old behaviour.
- The queue is bounded: under sustained log pressure lines are dropped and the count is reported in the log. (The legacy unbounded boost queue never dropped lines, but could grow without bound.)
- Exports removed from the installed headers of `bcos-utilities`: `FileLoggerHandler`, `StatFileLogger`, `StatFileLoggerHandler`, `c_statLogLevel`, `setStatLogLevel` — see `ChangeLog.md`.
- `BoostLogInitializer::initLog`'s `_logger` parameter is retained for source compatibility and ignored (channel filtering no longer exists). `log.enable=false` still disables logging.

## Tests

- New `LineAsyncSinkTest`: prefix layout for named and unnamed threads, spill past the 512 B inline arena, overflow drop counting with FATAL survival, backends that throw from `consumeLine`/`flush`, and FATAL delivery after `stop()`.
- `FIB184_LogSinkExceptionTest` now drives `LineAsyncSink` directly (it used to build its own `asynchronous_sink`, a path production no longer uses).
- `tools/.ci/ci_check_air.sh` no longer injects `flush=true`, so CI exercises the shipped default configuration again.

## Review follow-ups

`45ac218e` addresses the review findings: flush-on-drain with a bounded interval, synchronous FATAL with a flush before `std::abort()`, the `Unnamed-0x<tid>` thread field for unnamed threads, a 4 KB cap on pooled slot capacity, a dropped-lines counter and marker, `catch(...)` around backend `flush()`, the `LineAsyncSink` test suite with the FIB-184 case re-pointed at it, the `log.format` notice moved into the log file, the micro-cost items in `commitLine`/`appendLinePrefix`, and the breaking changes recorded in `ChangeLog.md`.
