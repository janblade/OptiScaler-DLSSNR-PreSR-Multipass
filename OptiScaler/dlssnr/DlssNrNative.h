#pragma once
#include <string>
#include <vector>
struct ID3D12GraphicsCommandList;
namespace DlssNrNative {
void* WrapNvapi(unsigned id,void* original);
void SetEnabled(bool enabled);
void SetPrecision(unsigned precision);
bool IsActive();
std::string Status();
// ViT reuse (see DlssNrVitReuse.h): bracket one model evaluation of `feature`. `every` = how often the ViT bottleneck is computed (1 = always).
void BeginEvaluate(const void* feature, bool reset, unsigned every, ID3D12GraphicsCommandList* cmd, bool profile);
void EndEvaluate(ID3D12GraphicsCommandList* cmd);
// Kernel census / per-group GPU timing lines finished since the last call (ini [DlssNr] KernelProfile), for the log.
std::vector<std::string> TakeProfileReports();
std::string VitStatus();
}
