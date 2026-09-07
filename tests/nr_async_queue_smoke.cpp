// Headless D3D12 scheduling test. Artificially stall only the worker queue and
// prove raster copies/completion can continue. Does not test NR visual quality.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 call failed"); }
void expect(bool b,const char* why) { if (!b) throw std::runtime_error(why); }
int main() try
{
    ComPtr<IDXGIFactory1> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; ComPtr<ID3D12Device> device;
    for (UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i) {
        DXGI_ADAPTER_DESC1 d {}; adapter->GetDesc1(&d);
        if (d.VendorId==0x10de && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))) break;
        adapter.Reset();
    }
    expect(device!=nullptr,"NVIDIA device unavailable");
    D3D12_COMMAND_QUEUE_DESC q {};
    ComPtr<ID3D12CommandQueue> raster,worker;
    check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&raster)));
    check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&worker)));
    ComPtr<ID3D12Fence> gate,workerDone,rasterDone;
    check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&workerDone)));
    check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&rasterDone)));
    // Always release the TEST-ONLY artificial gate, including on exceptions.
    struct Unblock { ID3D12Fence* gate; ~Unblock(){gate->Signal(1);} } unblock {gate.Get()};
    check(worker->Wait(gate.Get(),1)); check(worker->Signal(workerDone.Get(),1));
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> cmd;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd)));
    D3D12_RESOURCE_DESC bd {}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=256; bd.Height=1;
    bd.DepthOrArraySize=bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap {}; heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> input,output;
    check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&input)));
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&output)));
    unsigned* mapped=nullptr; check(input->Map(0,nullptr,(void**)&mapped));
    mapped[0]=101; mapped[1]=202; mapped[2]=303; input->Unmap(0,nullptr);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); expect(event!=nullptr,"Fence event");
    for (unsigned frame=0;frame<3;++frame) {
        cmd->CopyBufferRegion(output.Get(),frame*4,input.Get(),frame*4,4);
        check(cmd->Close()); ID3D12CommandList* list[]={cmd.Get()}; raster->ExecuteCommandLists(1,list);
        check(raster->Signal(rasterDone.Get(),frame+1)); check(rasterDone->SetEventOnCompletion(frame+1,event));
        expect(WaitForSingleObject(event,2000)==WAIT_OBJECT_0,"Raster stalled behind worker gate");
        expect(workerDone->GetCompletedValue()==0,"Worker unexpectedly completed before publication gate");
        if (frame<2) {check(allocator->Reset());check(cmd->Reset(allocator.Get(),nullptr));}
    }
    check(output->Map(0,nullptr,(void**)&mapped));
    expect(mapped[0]==101 && mapped[1]==202 && mapped[2]==303,"Raster did not advance independently");
    output->Unmap(0,nullptr);
    check(gate->Signal(1)); check(workerDone->SetEventOnCompletion(1,event));
    expect(WaitForSingleObject(event,2000)==WAIT_OBJECT_0,"Worker did not finish after gate release");
    CloseHandle(event);
    std::puts("PASS: raster advanced 3 frames while worker was stalled; worker completed only after release");
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
