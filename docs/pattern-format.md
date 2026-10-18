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

## Playback and suspend

Infinite four-step rise/hold/fall/off loops use the AW2023 pattern controllers
when every duration exactly matches a nominal chip timing value and each
channel rises from zero, holds its peak, falls to zero, then stays off.
This decision uses the data, never the pattern ID. The chip adds its minimum
40 ms startup delay and has oscillator tolerance. No repeating CPU timer or
keepalive is needed for these loops. All other timelines retain their file
timing and use software playback with an MCE CPU keepalive while animating.
Constant infinite output does not hold a CPU keepalive.

Preview uses software playback even for hardware-compatible patterns, so its
three-channel display and bounded lifetime share the same timeline. A failed
keepalive stops software output and retries after five seconds. Privacy or
cover removal cancels output and returns the hardware loan.

Register timing and linear transition mode follow the manufacturer
[AW2023 V1.4 datasheet](https://datasheet.lcsc.com/datasheet/pdf/9430e5da35f7279a291235190bd97521.pdf?productCode=C401014),
pages 17 and 19–20. Values that do not match are not rounded to the table.
