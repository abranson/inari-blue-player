#pragma once
#include "pattern.h"
#include <QPair>

namespace Inari {
using Registers = QVector<QPair<int, int>>;
// Compile only representable infinite rise/hold/fall/off loops. Other timelines
// retain their exact software timing instead of being silently quantised.
bool compileBreathing(const Pattern &pattern, Registers *registers);
}
