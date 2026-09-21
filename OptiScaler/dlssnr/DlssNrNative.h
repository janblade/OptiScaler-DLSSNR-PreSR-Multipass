#pragma once
#include <string>
namespace DlssNrNative {
void* WrapNvapi(unsigned id,void* original);
void SetEnabled(bool enabled);
void SetPrecision(unsigned precision);
bool IsActive();
std::string Status();
// ViT reuse (see DlssNrVitReuse.h): bracket one model evaluation of `feature`. `every` = how often the ViT bottleneck is computed (1 = always).
void BeginEvaluate(const void* feature, bool reset, unsigned every);
void EndEvaluate();
std::string VitStatus();
}
