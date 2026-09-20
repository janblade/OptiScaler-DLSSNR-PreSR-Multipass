// Host check of MfgUnlockMethod.h: which temporal fix the configuration selects.
// cl /std:c++20 /EHsc tests/mfg_method_smoke.cpp
#include "../OptiScaler/framegen/dlssg/MfgUnlockMethod.h"

#include <cstdio>

using namespace MfgUnlock;

static int fails = 0;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                \
            ++fails;                                                                                                   \
        }                                                                                                              \
    } while (0)

using Fix = std::optional<std::string>;
using Legacy = std::optional<bool>;

int main()
{
    // An old ini: neither key, or only the older on/off one. Unchanged behaviour.
    CHECK(ResolveTemporalMethod(std::nullopt, std::nullopt) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(std::nullopt, true) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(std::nullopt, false) == TemporalMethod::None);

    // A saved ini always carries AdaTemporalFix, "Auto" by default, so Auto has to leave the older key
    // in charge or a saved AdaBlackwellKernels=false would silently turn the unlock back on.
    CHECK(ResolveTemporalMethod(Fix("Auto"), std::nullopt) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(Fix("Auto"), true) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(Fix("Auto"), false) == TemporalMethod::None);

    // A named method wins over the older key.
    CHECK(ResolveTemporalMethod(Fix("Retarget"), std::nullopt) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(Fix("Retarget"), false) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(Fix("Ptx"), std::nullopt) == TemporalMethod::Ptx);
    CHECK(ResolveTemporalMethod(Fix("Ptx"), false) == TemporalMethod::Ptx);
    CHECK(ResolveTemporalMethod(Fix("Ptx"), true) == TemporalMethod::Ptx);

    // Anything else is Auto.
    CHECK(ResolveTemporalMethod(Fix(""), std::nullopt) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(Fix("ptx"), std::nullopt) == TemporalMethod::Retarget); // Config normalises case
    CHECK(ResolveTemporalMethod(Fix("Off"), std::nullopt) == TemporalMethod::Retarget);
    CHECK(ResolveTemporalMethod(Fix("Off"), false) == TemporalMethod::None);

    if (fails == 0)
        printf("mfg_method_smoke: all checks passed\n");
    return fails == 0 ? 0 : 1;
}
