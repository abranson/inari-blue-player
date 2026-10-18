#include "aw2023.h"

namespace Inari {
static int timingCode(qint64 us, bool ramp)
{
    // AW2023 V1.4, LEDxT0/T1 tables, pp.19-20. Code zero is different for
    // ramps and holds. Values are nominal; the chip oscillator has tolerance.
    static const int milliseconds[] = {40, 130, 260, 380, 510, 770, 1040, 1600,
                                       2100, 2600, 3100, 4200, 5200, 6200, 7300, 8300};
    for (int i = 0; i < 16; ++i)
        if (us == qint64(i == 0 && ramp ? 0 : milliseconds[i]) * 1000) return i;
    return -1;
}
bool compileBreathing(const Pattern &p, Registers *registers)
{
    if (p.repeat || p.steps.size() != 4) return false;
    int codes[4];
    for (int s = 0; s < 4; ++s) {
        codes[s] = timingCode(p.steps[s].duration, s == 0 || s == 2);
        if (codes[s] < 0) return false;
    }
    for (int c = 0; c < 3; ++c) {
        const int peak = p.steps[0].channels[c].target;
        for (int s = 0; s < 4; ++s) {
            const auto &r = p.steps[s].channels[c];
            if (r.delay || r.target != (s < 2 ? peak : 0)
                || r.fade != ((s == 0 || s == 2) ? p.steps[s].duration : 0)) return false;
        }
    }
    Registers result{{0x30, 0}};
    // Disable pattern mode while programming, then start all channels together.
    for (int c = 0; c < 3; ++c) result.append(qMakePair(0x31 + c, 0x0f));
    for (int c = 0; c < 3; ++c) {
        result.append(qMakePair(0x34 + c, p.steps[0].channels[c].target));
        result.append(qMakePair(0x37 + c * 3, (codes[0] << 4) | codes[1]));
        result.append(qMakePair(0x38 + c * 3, (codes[2] << 4) | codes[3]));
        result.append(qMakePair(0x39 + c * 3, 0));
    }
    for (int c = 0; c < 3; ++c) result.append(qMakePair(0x31 + c, 0x1f));
    // Linear PWM ramp matches the data format. Startup has the chip's minimum
    // 40 ms delay; no CPU wakeups are needed once this sequence is programmed.
    result.append(qMakePair(0x30, 0x0f));
    *registers = result;
    return true;
}
}
