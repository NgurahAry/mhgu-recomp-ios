// ---- page write tracking (plat_written_pages: the vertex cache and the texture checks in runtime/nvn/vulkan)
// Windows has GetWriteWatch. Here a query makes the blocks it reports read-only ("armed"); the first write to one faults
// into fault_handler, which makes it writable again and marks it written, and the next query over it reports it and arms
// it again. Blocks of 16 KB rather than 4 KB pages: a quarter of the faults, mprotect calls and map entries, and the same
// on a 16 KB-page kernel. Still reported as 4 KB pages (textures.c wants that granularity); a block a query covers only
// in part keeps its other pages' news for the queries that cover them (wt_pend), as GetWriteWatch resets only the range
// asked about.
//   untracked  never asked about (writable): all written, for all anyone knows
//   armed      read-only, not written since it was last reported
//   dirty      written since (writable again): reported, then armed again
//   hot        dirty at query after query (a dynamic buffer, a busy variable beside a vertex buffer): left writable and
//              reported every time, so its writers don't fault every frame; watched again after about a second
//              (staggered by block, a few dozen per query: not a burst of faults), twice as long each time it is hot
//              again straight away (up to ~8 s)
//   busy       changing: only the thread that made it busy changes its protection (a query arming it; a fault or a
//              host write making it writable)
//   off        plat_protect owns its protection: always reported, never armed
// Only blocks in backed chunks are armed (lazy_commit maps a chunk read-write over whatever was there), and only in the
// ranges the renderer asks about (heap textures and vertex buffers: not the stacks, TLS or the HLE heap). An armed
// block no query has covered for ~10 s (a buffer gone, a texture no longer drawn) is unwatched: writable, untracked.
// The kernel doesn't take these faults: a read() into an armed page fails with EFAULT (or stops short). File reads into
// guest memory go through plat_pread, and plat_commit disarms the buffers the host writes next (IPC buffers).
// Each read-only run can cost two of the process's vm.max_map_count entries (it splits a mapping): arming stops at a
// budget (the runs as last counted, + those armed and split since), and if mprotect runs out anyway (ENOMEM) where a
// write needs its page the tracking ends: every backed chunk writable again, every query "all written" (as without
// tracking: the renderer compares contents).
// MHGU_NO_WRITE_WATCH (any value): off. MHGU_WT_STATS or MHGU_VK_STATS: the counts in the log every 2 s.
#define WT_BLOCK 0x4000ull
#define WT_PAGES ((unsigned)(WT_BLOCK / PLAT_PAGE))
#define WT_ALL ((1u << WT_PAGES) - 1)
#define WT_BLOCKS (0x100000000ull / WT_BLOCK)
#define WT_STRIKES 4      // dirty at this many queries in a row, each within WT_COOL of the last: hot
#define WT_COOL 48        // in ticks of ~2 ms (plat_now_ns >> 21): ~100 ms
#define WT_REARM 512      // ticks a hot block stays unwatched: ~1 s, + 2 per (block number & 255) (staggered: up to ~1 s
                          // more), doubled each time it is hot again straight after a re-arm (WT_AGAIN)...
#define WT_BACKOFF 3      // ...this many times at most (~8.6 s)
#define WT_AGAIN 0x80     // (wt_back: re-armed from hot, not cool since)
#define WT_REARM_MAX 64   // hot blocks re-armed by one query at most (the others at the next ones)
#define WT_IDLE 10        // in ~1.07 s (plat_now_ns >> 30): an armed block no query has covered for longer is unwatched
enum { WT_UNTRACKED = 0, WT_ARMED, WT_DIRTY, WT_HOT, WT_BUSY, WT_OFF };
static _Atomic unsigned char wt_state[WT_BLOCKS];
static _Atomic unsigned char wt_pend[WT_BLOCKS];  // armed: its pages written before it was armed, not reported yet
static unsigned char wt_strikes[WT_BLOCKS];       // (these four, the run counts and the bounds: queries only - the
static uint16_t wt_time[WT_BLOCKS];               //  renderer, under its lock)
static unsigned char wt_back[WT_BLOCKS];          // (hot: the re-arm period's doublings, | WT_AGAIN)
static unsigned char wt_seen[WT_BLOCKS];          // (the last query over it, in ~1.07 s: WT_IDLE)
static _Atomic int g_wt = -1;                     // 1 on, 0 off, -1 not decided yet (the first query decides)
static _Atomic int g_wt_full, g_wt_dead;          // full: arming failed (out of map entries), none since; dead: ended
static int wt_runs, wt_max_runs;                  // read-only runs (as last counted, + those armed since) / budget
static _Atomic int wt_splits;                     // armed blocks made writable since that count (each may split a run)
static uint64_t wt_lo_b = ~0ull, wt_hi_b;         // blocks ever queried: [lo, hi)
static _Atomic uint64_t wt_n_query, wt_n_armed, wt_n_fault, wt_n_host, wt_n_efault, wt_n_idle;

static void wt_spin(int i) {
    if (i > 64) sched_yield();
    else atomic_thread_fence(memory_order_acquire);
}

// Out of map entries where a write needed its block writable: every backed chunk read-write again (that only joins
// mappings: it can't run out itself), and from now on every query answers "all written". Signal-safe.
static void wt_give_up(void) {
    atomic_store(&g_wt_dead, 1);
    uint64_t n = (g_hi - g_lo) / COMMIT_CHUNK;
    if (n > MAX_CHUNKS) n = MAX_CHUNKS;
    for (uint64_t c = 0; c < n;) {
        uint64_t e = c;
        while (e < n && atomic_load(&g_chunk[e]) == CHUNK_READY) e++;
        if (e > c) mprotect((void *)(uintptr_t)(g_lo + c * COMMIT_CHUNK), (e - c) * COMMIT_CHUNK, PROT_READ | PROT_WRITE);
        c = e + 1;
    }
}

// Blocks [rb, re) read-write, one mprotect (the caller has them busy). Signal-safe.
static void wt_rw(uint64_t rb, uint64_t re) {
    void *p = (void *)(uintptr_t)(g_lo + rb * WT_BLOCK);
    if (mprotect(p, (re - rb) * WT_BLOCK, PROT_READ | PROT_WRITE) != 0) {  // (ENOMEM: splitting the mapping needs an entry)
        wt_give_up();
        mprotect(p, (re - rb) * WT_BLOCK, PROT_READ | PROT_WRITE);
    }
}

// Block b writable again. 1: it was armed (now dirty: a write is on its way); 2 (heal): it was dirty or hot and is made
// writable all the same - a fault there raced whoever disarmed it, or hit a run whose arming failed halfway (mprotect
// may change part of a range before it fails); 0: none of those, not ours. Signal-safe: atomics, mprotect, sched_yield.
static int wt_unprotect(uint64_t b, bool heal) {
    for (int i = 0;; i++) {
        unsigned char s = atomic_load(&wt_state[b]);
        if (s == WT_BUSY) { wt_spin(i); continue; }
        if (s != WT_ARMED && (!heal || (s != WT_DIRTY && s != WT_HOT))) return 0;
        if (!atomic_compare_exchange_weak(&wt_state[b], &s, WT_BUSY)) continue;
        wt_rw(b, b + 1);
        if (s == WT_ARMED) atomic_fetch_add_explicit(&wt_splits, 1, memory_order_relaxed);  // (its run split, maybe)
        atomic_store(&wt_state[b], s == WT_ARMED ? WT_DIRTY : s);
        return s == WT_ARMED ? 1 : 2;
    }
}

static bool wt_backed(uint64_t off) {  // (off: from g_lo, inside the window) its chunk is backed
    return atomic_load(&g_chunk[off / COMMIT_CHUNK]) == CHUNK_READY;
}

// A SIGSEGV at host address a: a write to a block a query armed? True: it is writable now, resume the instruction.
static bool wt_fault(uint64_t a) {
    if (atomic_load_explicit(&g_wt, memory_order_relaxed) <= 0 || a < g_lo || a >= g_hi) return false;
    uint64_t off = a - g_lo, b = off / WT_BLOCK, next = (b + 1) * WT_BLOCK;
    // (a chunk not backed yet is lazy_commit's: none of it is armed, and a heal of the next block below would resume a
    // fault only backing this chunk ends, forever)
    if (!wt_backed(off)) return false;
    int r = wt_unprotect(b, true), armed = r == 1;
    // (an access across a block's end may be reported at its first byte: the read-only part is then the next block -
    // if its chunk is backed, else nothing of it is armed)
    if (off % WT_BLOCK >= WT_BLOCK - 64 && g_lo + next < g_hi && wt_backed(next)) {
        int r2 = wt_unprotect(b + 1, true);
        armed += r2 == 1;
        r |= r2;
    }
    if (armed) atomic_fetch_add_explicit(&wt_n_fault, (uint64_t)armed, memory_order_relaxed);
    return r != 0;
}

// The blocks host [addr, addr+size) touches inside the window: [*b0, *b1).
static bool wt_range(uint64_t addr, uint64_t size, uint64_t *b0, uint64_t *b1) {
    uint64_t end = addr + size;
    if (!size || end <= g_lo || addr >= g_hi) return false;
    if (addr < g_lo) addr = g_lo;
    if (end > g_hi) end = g_hi;
    *b0 = (addr - g_lo) / WT_BLOCK;
    *b1 = (end - g_lo + WT_BLOCK - 1) / WT_BLOCK;
    return true;
}

// Blocks [b0, b1): the armed ones writable again - claimed busy, then one mprotect per run of them, as arming does - and
// then `to`; the others `to` as well if all, else left as they are. The number that were armed.
static uint64_t wt_open(uint64_t b0, uint64_t b1, unsigned char to, bool all) {
    uint64_t rb = b0, re = b0, n = 0;  // claimed, still read-only: [rb, re)
    for (uint64_t b = b0; b <= b1; b++) {
        bool claim = false;
        for (int i = 0; b < b1; i++) {  // (busy: another thread's for a moment; claims go up, so none waits on ours)
            unsigned char s = atomic_load(&wt_state[b]);
            if (s == WT_BUSY) { wt_spin(i); continue; }
            if (s != WT_ARMED && (!all || s == to)) break;
            if (atomic_compare_exchange_weak(&wt_state[b], &s, s == WT_ARMED ? WT_BUSY : to)) { claim = s == WT_ARMED; break; }
        }
        if (claim) {
            if (re == rb) rb = b;
            re = b + 1;
        } else if (re > rb) {
            wt_rw(rb, re);
            atomic_fetch_add_explicit(&wt_splits, 1, memory_order_relaxed);  // (a run split, maybe)
            for (uint64_t k = rb; k < re; k++) atomic_store(&wt_state[k], to);
            n += re - rb;
            rb = re;
        }
    }
    return n;
}

// The host writes [addr, addr+size) next, maybe through the kernel (which can't take the fault): the armed blocks there
// writable again, as written.
static void wt_disarm(uint64_t addr, uint64_t size) {
    uint64_t b0, b1;
    if (atomic_load(&g_wt) <= 0 || !wt_range(addr, size, &b0, &b1)) return;
    uint64_t n = wt_open(b0, b1, WT_DIRTY, false);
    if (n) atomic_fetch_add_explicit(&wt_n_host, n, memory_order_relaxed);
}

// plat_protect: those blocks' protection is the caller's now. Armed ones writable first: the caller may change only
// part of a block, and a write to the rest of it would fault with nobody to take it.
static void wt_forget(uint64_t addr, uint64_t size) {
    uint64_t b0, b1;
    if (wt_range(addr, size, &b0, &b1)) wt_open(b0, b1, WT_OFF, true);
}

static unsigned wt_in(uint64_t base, uint64_t lo, uint64_t hi) {  // the pages of the block at base inside [lo, hi)
    unsigned m = 0;
    for (unsigned p = 0; p < WT_PAGES; p++)
        if (base + p * PLAT_PAGE >= lo && base + (p + 1) * PLAT_PAGE <= hi) m |= 1u << p;
    return m;
}

// A query found block b dirty. True: it has been at query after query, and is hot now (left writable).
static bool wt_heat(uint64_t b, uint16_t t) {
    uint16_t d = (uint16_t)(t - wt_time[b]);
    if (!wt_strikes[b] || d > WT_COOL) wt_strikes[b] = 1, wt_time[b] = t, wt_back[b] = 0;  // (the first write in a while)
    else if (d >= 2 && wt_strikes[b] < WT_STRIKES) wt_strikes[b]++, wt_time[b] = t;  // (queries within ~4 ms: once)
    unsigned char e = WT_DIRTY;
    if (wt_strikes[b] < WT_STRIKES || !atomic_compare_exchange_strong(&wt_state[b], &e, WT_HOT)) return false;
    unsigned char k = wt_back[b] & ~WT_AGAIN;  // (hot again straight after its re-arm: left alone twice as long)
    wt_back[b] = (unsigned char)((wt_back[b] & WT_AGAIN) && k < WT_BACKOFF ? k + 1 : k);
    return true;
}

static unsigned wt_rearm_ticks(uint64_t b) {  // how long hot block b stays unwatched (staggered: not all at once)
    return ((unsigned)WT_REARM << (wt_back[b] & ~WT_AGAIN)) + (unsigned)(b & 255) * 2;
}

// Blocks [rb, re), busy (claimed by a query over host [lo, hi)): read-only together, armed. Their pages outside the query
// stay pending - written, for all anyone knows - until a query over them reports them.
static void wt_arm(uint64_t rb, uint64_t re, uint64_t lo, uint64_t hi) {
    bool dead = atomic_load(&g_wt_dead);
    bool ok = !dead && mprotect((void *)(uintptr_t)(g_lo + rb * WT_BLOCK), (re - rb) * WT_BLOCK, PROT_READ) == 0;
    int err = errno;  // (logged below, once the blocks are no longer busy: a fault on one waits for that)
    for (uint64_t b = rb; b < re; b++) {
        if (ok) atomic_store(&wt_pend[b], (unsigned char)(WT_ALL & ~wt_in(g_lo + b * WT_BLOCK, lo, hi)));
        atomic_store(&wt_state[b], ok ? WT_ARMED : WT_DIRTY);  // (failed: any part made read-only anyway heals on a fault)
    }
    if (ok) {
        wt_runs++;
        atomic_fetch_add_explicit(&wt_n_armed, re - rb, memory_order_relaxed);
    } else if (!dead && !atomic_exchange(&g_wt_full, 1)) {  // (ENOMEM: out of map entries; anything else: no use retrying)
        fprintf(stderr, "[plat] write tracking: mprotect failed (%s) at ~%d read-only runs: no more blocks watched\n",
                strerror(err), wt_runs);
    }
}

static void wt_init(void) {
    int on = 0;
#if (defined(MHGU_IOS) || defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)) && !defined(MHGU_IOS_WRITE_WATCH)
    // The iOS app build explicitly defines MHGU_IOS. Do not rely only on Clang's
    // deployment-target macro here: some packaging/build paths omit it, which
    // would silently re-enable mprotect-based write tracking on iOS. A separate
    // diagnostic build can opt in with MHGU_IOS_WRITE_WATCH after graphics fixes.
    fprintf(stderr, "[plat] page write tracking off (MHGU_IOS conservative path)\n");
#else
    long pg = sysconf(_SC_PAGESIZE);
#ifdef __APPLE__
    // iOS does not expose Linux's vm.max_map_count. Keep this conservative: at most 2048
    // read-only runs (at most 4096 extra VM regions), with automatic fallback if mprotect fails.
    wt_max_runs = 2048;
#else
    int mmc = 65530;
    FILE *f = fopen("/proc/sys/vm/max_map_count", "r");
    if (f) {
        if (fscanf(f, "%d", &mmc) != 1) mmc = 65530;
        fclose(f);
    }
    // (two map entries per read-only run at worst; 25000 left for the rest: the GPU driver maps every allocation)
    wt_max_runs = (mmc - 25000) / 2;
#endif
    if (getenv("MHGU_NO_WRITE_WATCH"))
        fprintf(stderr, "[plat] page write tracking off (MHGU_NO_WRITE_WATCH)\n");
    else if (pg <= 0 || (uint64_t)pg > WT_BLOCK || !g_lo || (g_lo & (WT_BLOCK - 1)) || g_hi - g_lo > WT_BLOCKS * WT_BLOCK)
        fprintf(stderr, "[plat] page write tracking off (page size %ld)\n", pg);
    else if (wt_max_runs < 1024)
        fprintf(stderr, "[plat] page write tracking off (VM run budget %d)\n", wt_max_runs);
    else {
        on = 1;
        fprintf(stderr, "[plat] page write tracking on: 16 KB blocks, up to %d read-only runs\n", wt_max_runs);
    }
#endif
    atomic_store(&g_wt, on);
}

static void wt_unwatch(uint64_t rb, uint64_t re) {  // (busy, armed, idle: writable and untracked again)
    wt_rw(rb, re);
    for (uint64_t b = rb; b < re; b++) atomic_store(&wt_state[b], WT_UNTRACKED);
    atomic_fetch_add_explicit(&wt_n_idle, re - rb, memory_order_relaxed);
}

// Now and then (from a query): the armed blocks no query has covered for WT_IDLE unwatched, the read-only runs counted
// afresh (faults split them, arming joins them; in between each new run and each split counts one), and with
// MHGU_WT_STATS / MHGU_VK_STATS the counts in the log every 2 s.
static void wt_tick(uint64_t now) {
    static uint64_t next, next_log;
    static int stats = -1, said_dead;
    if (now < next) return;
    next = now + 250000000ull;
    if (stats < 0) {
#ifdef MHGU_IOS_VK_STATS
        stats = 1;
#else
        stats = getenv("MHGU_WT_STATS") || getenv("MHGU_VK_STATS");
#endif
    }
    atomic_store(&wt_splits, 0);  // (before the count: a split while it runs is counted twice at worst, never missed)
    unsigned char g = (unsigned char)(now >> 30);
    int runs = 0, armed = 0, hot = 0, dirty = 0;
    bool ro = false;
    uint64_t ub = 0, ue = 0;  // idle armed blocks claimed (busy) to be unwatched: [ub, ue)
    for (uint64_t b = wt_lo_b; b < wt_hi_b; b++) {
        unsigned char s = atomic_load_explicit(&wt_state[b], memory_order_relaxed), e = WT_ARMED;
        if (s == WT_ARMED && (unsigned char)(g - wt_seen[b]) > WT_IDLE &&
            atomic_compare_exchange_strong(&wt_state[b], &e, WT_BUSY)) {
            if (ue == ub) ub = b;
            ue = b + 1;
            s = WT_UNTRACKED;  // (counted as it will be)
        } else if (ue > ub) {
            wt_unwatch(ub, ue);
            ub = ue;
        }
        bool r = s == WT_ARMED || s == WT_BUSY;
        runs += r && !ro;
        ro = r;
        armed += s == WT_ARMED;
        hot += s == WT_HOT;
        dirty += s == WT_DIRTY;
    }
    if (ue > ub) wt_unwatch(ub, ue);
    wt_runs = runs;
    if (atomic_load(&g_wt_dead) && !said_dead) {
        said_dead = 1;
        fprintf(stderr, "[plat] write tracking: out of map entries (vm.max_map_count) where a write needed its page: "
                        "stopped, every page counts as written from now on\n");
    }
    if (!stats || now < next_log) return;
    next_log = now + 2000000000ull;
    fprintf(stderr, "[plat] 2s write tracking: %llu queries, %llu blocks armed, %llu write faults, %llu host writes, "
                    "%llu unwatched (idle), %llu EFAULT retries | %d armed in %d runs (budget %d), %d hot, %d dirty%s%s\n",
            (unsigned long long)atomic_exchange(&wt_n_query, 0), (unsigned long long)atomic_exchange(&wt_n_armed, 0),
            (unsigned long long)atomic_exchange(&wt_n_fault, 0), (unsigned long long)atomic_exchange(&wt_n_host, 0),
            (unsigned long long)atomic_exchange(&wt_n_idle, 0), (unsigned long long)atomic_exchange(&wt_n_efault, 0),
            armed, runs, wt_max_runs, hot, dirty,
            atomic_load(&g_wt_full) ? ", arming stopped (mprotect failed)" : "",
            atomic_load(&g_wt_dead) ? ", STOPPED" : "");
}

// Reports what GetWriteWatch would, in 4 KB pages, and arms what it reports (see "page write tracking" above).
int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, wt_init);
    if (atomic_load(&g_wt) <= 0 || addr < g_lo || addr > g_hi || size > g_hi - addr || max <= 0) return -1;
    *page = PLAT_PAGE;
    uint64_t now = plat_now_ns();
    wt_tick(now);
    if (!size) return 0;
    uint64_t lo = addr & ~(PLAT_PAGE - 1), hi = (addr + size + PLAT_PAGE - 1) & ~(PLAT_PAGE - 1);
    int n = 0;
    if (atomic_load(&g_wt_dead)) {  // (the tracking ended: all of it written)
        for (uint64_t p = lo; p < hi && n < max; p += PLAT_PAGE) pages[n++] = p;
        return n;
    }
    atomic_fetch_add_explicit(&wt_n_query, 1, memory_order_relaxed);
    uint16_t t = (uint16_t)(now >> 21);
    unsigned char g = (unsigned char)(now >> 30);
    uint64_t b0 = (lo - g_lo) / WT_BLOCK, b1 = (hi - g_lo + WT_BLOCK - 1) / WT_BLOCK;
    if (b0 < wt_lo_b) wt_lo_b = b0;
    if (b1 > wt_hi_b) wt_hi_b = b1;
    uint64_t rb = 0, re = 0;  // blocks claimed for arming (busy) and not read-only yet: [rb, re), one mprotect
    int rearmed = 0;          // hot blocks re-armed (WT_REARM_MAX)
    bool over = false;        // arming held back by the map budget
    for (uint64_t b = b0; b < b1; b++) {
        uint64_t base = g_lo + b * WT_BLOCK;
        unsigned in = base >= lo && base + WT_BLOCK <= hi ? WT_ALL : wt_in(base, lo, hi), rep = 0;
        unsigned char s = WT_UNTRACKED;
        bool arm = false;
        if (atomic_load(&g_chunk[(base - g_lo) / COMMIT_CHUNK]) == CHUNK_READY) {  // (else never backed: zeros, unwritten)
            wt_seen[b] = g;
            s = atomic_load(&wt_state[b]);
            switch (s) {
            case WT_ARMED:  // (only what was written before it was armed and no query has reported yet)
                rep = atomic_load(&wt_pend[b]) & in;
                if (rep) atomic_fetch_and(&wt_pend[b], (unsigned char)~rep);
                break;
            case WT_UNTRACKED:
                rep = in, arm = true;
                wt_strikes[b] = 0, wt_time[b] = t, wt_back[b] = 0;
                break;
            case WT_DIRTY: rep = in, arm = !wt_heat(b, t); break;
            case WT_HOT:  // (its time is up: watched again, and hot again at its next dirty query)
                rep = in;
                arm = (uint16_t)(t - wt_time[b]) >= wt_rearm_ticks(b) && rearmed < WT_REARM_MAX;
                break;
            default: rep = in; break;  // (busy, off: written, for all anyone knows)
            }
        }
        for (unsigned p = 0; p < WT_PAGES; p++)  // (a full array: the caller takes the whole range as written)
            if (rep >> p & 1 && n < max) pages[n++] = base + p * PLAT_PAGE;
        bool claim = false;
        if (arm && !atomic_load(&g_wt_full)) {
            if (re > rb || wt_runs + atomic_load_explicit(&wt_splits, memory_order_relaxed) < wt_max_runs) {
                unsigned char e = s;
                claim = atomic_compare_exchange_strong(&wt_state[b], &e, WT_BUSY);
            } else {
                over = true;
            }
        }
        if (claim && s == WT_HOT) rearmed++, wt_strikes[b] = WT_STRIKES - 1, wt_time[b] = t, wt_back[b] |= WT_AGAIN;
        if (claim) {
            if (re == rb) rb = b;
            re = b + 1;
        } else if (re > rb) {
            wt_arm(rb, re, lo, hi);
            rb = re;
        }
    }
    if (re > rb) wt_arm(rb, re, lo, hi);
    static bool said_over;
    if (over && !said_over) {  // (here: no block busy)
        said_over = true;
        fprintf(stderr, "[plat] write tracking: budget of %d read-only runs (vm.max_map_count) reached: more blocks are "
                        "armed only as others are written or go idle\n", wt_max_runs);
    }
    return n;
}

// pread for buffers that may be guest memory: an armed page there makes the kernel fail (EFAULT) or stop short where a
// store would just fault, so the range is disarmed first and again on EFAULT (a query may arm it meanwhile), and a short
// read goes on (only the end of the file stops it). Refused again and again: through a buffer of ours, whose copy into
// guest memory faults like any store - only into guest memory: anywhere else EFAULT is a bad pointer, the caller's.
ssize_t plat_pread(int fd, void *buf, size_t n, off_t off) {
    uint64_t a = (uint64_t)(uintptr_t)buf;
    // (not decided yet, -1: the careful way, the first query may be arming right now)
    if (!atomic_load(&g_wt) || a >= g_hi || a + n <= g_lo) return pread(fd, buf, n, off);
    size_t done = 0;
    for (int refused = 0; done < n;) {
        wt_disarm(a + done, n - done);
        ssize_t r = pread(fd, (char *)buf + done, n - done, off + (off_t)done);
        if (r > 0) { done += (size_t)r; refused = 0; continue; }
        if (r == 0) break;
        if (errno == EINTR) continue;
        if (errno != EFAULT) return done ? (ssize_t)done : -1;
        atomic_fetch_add_explicit(&wt_n_efault, 1, memory_order_relaxed);
        if (++refused < 8) continue;
        char tmp[4096];
        uint64_t d = a + done;
        size_t len = n - done < sizeof tmp ? n - done : sizeof tmp;
        if (d < g_lo || d >= g_hi) { errno = EFAULT; return done ? (ssize_t)done : -1; }
        if (len > g_hi - d) len = (size_t)(g_hi - d);  // (up to the window's end: past it, not ours)
        r = pread(fd, tmp, len, off + (off_t)done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return done ? (ssize_t)done : r;
        memcpy((char *)buf + done, tmp, (size_t)r);
        done += (size_t)r;
        refused = 0;
    }
    return (ssize_t)done;
}

