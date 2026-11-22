// Entry of the game code library (runtime/gamelib.h), compiled into it with the generated code: the function
// tables, found by name when the app loads the library.
#include "gamelib.h"

// weak fallbacks keep the executable/linkable entry table available until generated/
// is supplied with a dump (it is intentionally absent from source checkouts).
__attribute__((weak)) const uint32_t main_fn_addrs[] = {0};
__attribute__((weak)) const uint32_t sdk_fn_addrs[] = {0};
__attribute__((weak)) const uint32_t main_fn_count = 0;
__attribute__((weak)) const uint32_t sdk_fn_count = 0;
__attribute__((weak)) const GuestFn main_fn_ptrs[] = {0};
__attribute__((weak)) const GuestFn sdk_fn_ptrs[] = {0};

GAMELIB_EXPORT const GameLibTables *mhgugame_tables(void) {
    static const GameLibTables t = {GAMELIB_ABI,  GAMELIB_ID,   main_fn_addrs,  sdk_fn_addrs,
                                    main_fn_ptrs, sdk_fn_ptrs,  &main_fn_count, &sdk_fn_count};
    return &t;
}
