# Inari Blue pattern format

Pattern files are UTF-8 JSON objects with these fields:

```json
{
  "version": 1,
  "id": "steady",
  "name": "Steady light",
  "repeat": 0,
  "steps": [
    {
      "duration_ms": 1000,
      "channels": [
        { "to": 255 },
        { "to": 255 },
        { "to": 255 }
      ]
    }
  ]
}
```

There are exactly three channels per step. `duration_ms`, `delay_ms`, and
`fade_ms` are finite non-negative millisecond values; fractional values are
allowed and are rounded to microseconds internally. Each step duration is at
least 1 ms, each channel target is an integer brightness from 0 through 255,
and a channel's delay plus fade must fit within its step. A fade starts from
the previous channel target. A missing delay or fade is zero.

`repeat: 0` means repeat forever. A positive repeat count completes after that
many cycles and then returns an all-zero frame. Playback starts from all-zero
levels. At a cycle boundary, the next cycle starts from the previous cycle's
last targets, so fades and loops do not reset channels unexpectedly. Delays,
fades, and their endpoints are evaluated independently for each channel.

The parser accepts files up to 64 KiB, with at most 256 steps, at most five
minutes per cycle, and at most 60 seconds per step. The format does not promise
hardware-specific optimization; the current player evaluates frames in
software.

The ID `off` is reserved for disabling a category. Its playback is always dark.
