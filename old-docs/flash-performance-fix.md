# Flash Performance Fix (historical)

**Superseded, July 2026.** This document described a RAM-buffering patch for
the original 2025 `appendRecord()` implementation. That entire implementation
has since been replaced — see `flash-feature.md` for the current design and
`bugs-and-suggestions.md` for why.

The concern this file addressed (write amplification from programming a 4KB
sector per 224-byte record) is solved structurally in the rewrite:

- Records accumulate in a 4KB RAM assembly buffer and are programmed into the
  open head sector in chunks — typically one program op per sector fill
  (~17 records / ~17s at 1Hz), plus an age-based flush at ~15s to bound RAM
  loss on power cuts.
- Each sector receives exactly one erase per ring cycle, when it is opened.
- Reading/spooling performs no writes at all until a fully-consumed sector is
  reclaimed with a single erase.
