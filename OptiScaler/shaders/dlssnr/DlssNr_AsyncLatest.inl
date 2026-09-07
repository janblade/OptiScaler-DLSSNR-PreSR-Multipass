// Included inside namespace DlssNr. CPU/API recording is serialized by g_nrMutex;
// expensive GPU execution is on a private queue. The raster queue NEVER waits on it.
namespace AsyncLatest
{
constexpr unsigned MarkerCount = 32, ResultCount = 3;
struct Context
{
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue *rasterQueue = nullptr, *workQueue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* commands = nullptr;
    ID3D12Fence* done = nullptr;
    ID3D12Fence* inputReady = nullptr;
    ID3D12QueryHeap* queries = nullptr;
    ID3D12Resource *readback = nullptr, *color = nullptr, *depth = nullptr, *motion = nullptr,
                   *exposure = nullptr, *dummyOutput = nullptr, *clean = nullptr, *composed = nullptr;
    ID3D12Resource* results[ResultCount] {};
    unsigned resultUse[ResultCount] {};
    bool resultUsed[ResultCount] {}, markerUsed[MarkerCount] {};
    volatile UINT64* completed = nullptr;
    unsigned nextMarker = 0, captureMarker = 0;
    unsigned w = 0, h = 0, ow = 0, oh = 0, flags = 0;
    D3D12_RESOURCE_DESC depthDesc {}, motionDesc {}, exposureDesc {};
    DXGI_FORMAT colorFormat {}, outputFormat {};
    bool hasMotion = false, hasExposure = false, failed = false, produced = false;
    enum Phase { Idle, Capturing, Running } phase = Idle;
    int latest = -1, writing = -1;
    UINT64 serial = 0, revision = 0, capturedRevision = 0, publishedEpoch = 0, capturedEpoch = 0;
    UINT64 published = 0, dropped = 0;
    bool sawFrame = false;
    UINT64 lastEpoch = 0;
    NVSDK_NGX_Parameter* parameters = nullptr;
    std::unique_ptr<DlssNr_Dx12> codec;
    std::unique_ptr<DlssNr_Dx12> jobCodec; // descriptors/constants never shared across GPU queues

    bool Complete() const
    {
        if (done && done->GetCompletedValue() < serial) return false;
        if (completed) for (unsigned i=0;i<MarkerCount;++i) if (markerUsed[i] && !completed[i]) return false;
        return true;
    }
    ~Context()
    {
        if (parameters && NVNGXProxy::D3D12_DestroyParameters()) NVNGXProxy::D3D12_DestroyParameters()(parameters);
        if (readback && completed) readback->Unmap(0, nullptr);
        for (auto* r : {readback,color,depth,motion,exposure,dummyOutput,clean,composed,results[0],results[1],results[2]})
            if (r) r->Release();
        if (commands) commands->Release(); if (allocator) allocator->Release();
        if (queries) queries->Release(); if (done) done->Release();
        if (inputReady) inputReady->Release();
        if (workQueue) workQueue->Release(); if (rasterQueue) rasterQueue->Release(); if (device) device->Release();
    }
    int ReserveMarker()
    {
        unsigned i=nextMarker;
        if (markerUsed[i] && !completed[i]) return -1;
        completed[i]=0; markerUsed[i]=true; nextMarker=(i+1)%MarkerCount;
        return (int)i;
    }
    void Mark(ID3D12GraphicsCommandList* cmd, unsigned i)
    {
        cmd->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,i);
        cmd->ResolveQueryData(queries,D3D12_QUERY_TYPE_TIMESTAMP,i,1,readback,i*sizeof(UINT64));
    }
    int WritableResult() const
    {
        for (unsigned i=0;i<ResultCount;++i)
            if ((int)i!=latest && (!resultUsed[i] || completed[resultUse[i]])) return (int)i;
        return -1;
    }
};
std::unique_ptr<Context> current;
std::vector<std::unique_ptr<Context>> retired;
std::string status = "not started";
struct Pending
{
    ID3D12GraphicsCommandList* cmd = nullptr;
    NVSDK_NGX_Parameter* parameters = nullptr;
    ID3D12Resource* output = nullptr;
    UINT64 epoch = 0;
    float preExposure = 1;
} pending;

void Say(const std::string& s)
{
    if (status==s) return;
    status=s; LOG_INFO("DLSS-NR async latest: {}",s);
}
void Collect() { std::erase_if(retired,[](const auto& c){return c->Complete();}); }
void Cancel()
{
    pending={};
    if (current) retired.push_back(std::move(current));
    Collect();
}
bool Draining() { Collect(); return !retired.empty(); }
bool Shutdown()
{
    Cancel();
    const bool safe=retired.empty();
    // No shutdown wait on a possibly unsubmitted game list; keep in-flight objects alive.
    for (auto& c:retired) (void)c.release();
    retired.clear(); return safe;
}

ID3D12Resource* Texture(ID3D12Device* device, DXGI_FORMAT format, unsigned w, unsigned h, bool uav)
{
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc=CD3DX12_RESOURCE_DESC::Tex2D(format,w,h,1,1,1,0,
        uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* resource=nullptr;
    device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource));
    return resource;
}
bool Allocate(Context& c)
{
    D3D12_COMMAND_QUEUE_DESC q {}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    // Normal priority: do not raise NR over the game's graphics queue.
    if (FAILED(c.device->CreateCommandQueue(&q,IID_PPV_ARGS(&c.workQueue))) ||
        FAILED(c.device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&c.allocator))) ||
        FAILED(c.device->CreateCommandList(0,q.Type,c.allocator,nullptr,IID_PPV_ARGS(&c.commands))) ||
        FAILED(c.commands->Close()) || FAILED(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&c.done))) ||
        FAILED(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&c.inputReady)))) return false;
    c.color=Texture(c.device,c.colorFormat,c.w,c.h,false);
    c.depth=Texture(c.device,c.depthDesc.Format,(unsigned)c.depthDesc.Width,c.depthDesc.Height,false);
    c.motion=Texture(c.device,c.hasMotion?c.motionDesc.Format:DXGI_FORMAT_R16G16_FLOAT,
                     c.hasMotion?(unsigned)c.motionDesc.Width:c.w,c.hasMotion?c.motionDesc.Height:c.h,!c.hasMotion);
    if (c.hasExposure) c.exposure=Texture(c.device,c.exposureDesc.Format,(unsigned)c.exposureDesc.Width,c.exposureDesc.Height,false);
    c.dummyOutput=Texture(c.device,c.outputFormat,c.ow,c.oh,true);
    c.clean=Texture(c.device,c.outputFormat,c.ow,c.oh,false);
    c.composed=Texture(c.device,c.outputFormat,c.ow,c.oh,true);
    for (auto& r:c.results) r=Texture(c.device,DXGI_FORMAT_R16G16B16A16_FLOAT,c.ow,c.oh,false);
    if (!c.color || !c.depth || !c.motion || (c.hasExposure && !c.exposure) || !c.dummyOutput || !c.clean ||
        !c.composed || !c.results[0] || !c.results[1] || !c.results[2]) return false;
    c.codec=std::make_unique<DlssNr_Dx12>("Async NR exchange",c.device);
    c.jobCodec=std::make_unique<DlssNr_Dx12>("Async NR private guides",c.device);
    if (!c.codec->IsInit() || !c.jobCodec->IsInit()) return false;
    D3D12_QUERY_HEAP_DESC query {}; query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; query.Count=MarkerCount;
    if (FAILED(c.device->CreateQueryHeap(&query,IID_PPV_ARGS(&c.queries)))) return false;
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto desc=CD3DX12_RESOURCE_DESC::Buffer(MarkerCount*sizeof(UINT64));
    if (FAILED(c.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr,IID_PPV_ARGS(&c.readback)))) return false;
    void* mapped=nullptr; if (FAILED(c.readback->Map(0,nullptr,&mapped))) return false;
    c.completed=(volatile UINT64*)mapped; for (unsigned i=0;i<MarkerCount;++i) c.completed[i]=0;
    if (!NVNGXProxy::InitDx12(c.device) || !NVNGXProxy::D3D12_AllocateParameters()) return false;
    return NVNGXProxy::D3D12_AllocateParameters()(&c.parameters)==NVSDK_NGX_Result_Success && c.parameters;
}

void Pump(Context& c)
{
    if (c.failed) return;
    if (c.done->GetCompletedValue()==UINT64_MAX)
    { c.failed=true; c.latest=-1; Say("device removed; current raster retained"); return; }
    if (c.phase==Context::Running && c.done->GetCompletedValue()>=c.serial)
    {
        if (c.produced && c.capturedRevision==c.revision)
        { c.latest=c.writing; c.publishedEpoch=c.capturedEpoch; ++c.published; }
        c.phase=Context::Idle;
    }
    if (c.phase!=Context::Capturing || !c.completed[c.captureMarker]) return;
    if (c.capturedRevision!=c.revision) { c.phase=Context::Idle; return; }
    if (FAILED(c.allocator->Reset()) || FAILED(c.commands->Reset(c.allocator,nullptr)))
    { c.failed=true; Say("command-list reset failed; retaining latest completed result"); return; }
    ScopedNrStateEnvelope envelope(c.commands);
    for (auto* r:{c.color,c.depth,c.motion,c.exposure})
        if (r) Barrier(c.commands,r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (!c.hasMotion)
    {
        Barrier(c.commands,c.motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DlssNrConstants zero {}; zero.Mode=DlssNrMode_ZeroMotion; zero.Width=c.w; zero.Height=c.h;
        c.jobCodec->DispatchPass(c.commands,zero,c.color,nullptr,nullptr,nullptr,nullptr,c.motion,nullptr);
        Barrier(c.commands,c.motion,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    // NR/SR histories reset for each sparse snapshot; never pretend its MVs span all skipped frames.
    DeferredSr::Before(c.commands,c.parameters,c.serial+1,c.workQueue,true);
    c.produced=DeferredSr::ResolvePrivate(c.commands,c.parameters,c.serial+1,c.results[c.writing]);
    if (g_nr.failed || (DeferredSr::current && DeferredSr::current->failed))
    { c.failed=true; Say("NR/private DLSS failed; no new jobs, retaining last completed residual"); }
    for (auto* r:{c.color,c.depth,c.motion,c.exposure})
        if (r) Barrier(c.commands,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    if (FAILED(c.commands->Close())) { c.failed=true; Say("NR command list failed to close"); return; }
    // The marker proves the capture list was submitted. Establish explicit cross-queue
    // ordering for input copies and any old consumers of the writable result slot.
    // This dependency is ONLY worker -> raster; the raster never waits for NR.
    if (FAILED(c.rasterQueue->Signal(c.inputReady,c.serial+1)) || FAILED(c.workQueue->Wait(c.inputReady,c.serial+1)))
    { c.failed=true; Say("NR input handoff failed; current raster retained"); return; }
    ID3D12CommandList* lists[]={c.commands}; c.workQueue->ExecuteCommandLists(1,lists);
    ++c.serial;
    if (FAILED(c.workQueue->Signal(c.done,c.serial))) { c.failed=true; Say("NR completion signal failed"); return; }
    c.phase=Context::Running;
}

bool ValidTexture(ID3D12Resource* r)
{
    if (!r) return false;
    const auto d=r->GetDesc();
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.DepthOrArraySize==1 &&
           d.MipLevels==1 && d.SampleDesc.Count==1 && d.Width>0 && d.Height>0;
}
bool SameTexture(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b)
{ return a.Width==b.Width && a.Height==b.Height && a.Format==b.Format; }

void Before(ID3D12GraphicsCommandList* cmd,NVSDK_NGX_Parameter* p,UINT64 epoch,ID3D12CommandQueue* queue)
{
    using DeferredSr::UInt; using DeferredSr::Float;
    pending={}; Collect();
    const auto& cfg=*Config::Instance();
    if (!cmd || cmd->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
        ((cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default()) &&
         !D3D12Hooks::CanRestoreRootSignature(cmd))) { Say("inactive: unsupported game command list"); return; }
    if (cfg.DlssNrUseProxy.value_or_default() || cfg.DlssNrHoldFrame.value_or_default() ||
        cfg.DlssNrDebugView.value_or_default() || cfg.DlssNrCompare.value_or_default() ||
        cfg.DlssNrShowSkinMask.value_or_default() || !cfg.DlssNrApplyModel.value_or_default())
    { Cancel(); Say("inactive: disable proxy/hold/debug/compare and enable Apply model"); return; }
    auto* color=GetResource(p,NVSDK_NGX_Parameter_Color,"DLSSD.Color");
    auto* output=GetResource(p,NVSDK_NGX_Parameter_Output,"DLSSD.Output");
    auto* depth=GetResource(p,NVSDK_NGX_Parameter_Depth,"DLSSD.Depth");
    auto* motion=GetResource(p,NVSDK_NGX_Parameter_MotionVectors,"DLSSD.MotionVectors");
    auto* exposure=GetResource(p,NVSDK_NGX_Parameter_ExposureTexture,"ExposureTexture");
    if (!ValidTexture(color) || !ValidTexture(output) || !ValidTexture(depth) || color==output ||
        (motion && !ValidTexture(motion)) || (exposure && !ValidTexture(exposure)))
    { Cancel(); Say("inactive: missing/unsupported colour, output or guide texture"); return; }
    for (const char* key:{NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,
         NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,
         NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y,
         NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y})
        if (UInt(p,key)) { Cancel(); Say("inactive: nonzero resource offsets"); return; }
    auto cd=color->GetDesc(),od=output->GetDesc(),dd=depth->GetDesc();
    auto md=motion?motion->GetDesc():D3D12_RESOURCE_DESC {}, ed=exposure?exposure->GetDesc():D3D12_RESOURCE_DESC {};
    auto active=PreSrColorExtent(cd,UInt(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width),
                                 UInt(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height));
    if (!active || active->width>od.Width || active->height>od.Height || dd.Width<active->width || dd.Height<active->height)
    { Cancel(); Say("inactive: invalid active dimensions"); return; }
    auto* rasterQueue=queue?queue:(ID3D12CommandQueue*)State::Instance().currentCommandQueue;
    ID3D12Device *device=nullptr,*queueDevice=nullptr;
    if (FAILED(cmd->GetDevice(IID_PPV_ARGS(&device)))) return;
    bool same=rasterQueue && rasterQueue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT &&
        SUCCEEDED(rasterQueue->GetDevice(IID_PPV_ARGS(&queueDevice))) && queueDevice==device;
    if (queueDevice) queueDevice->Release();
    if (!same) { device->Release(); Say("waiting for matching raster queue"); return; }
    unsigned flags=UInt(p,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags);
    if (!motion) flags=(flags & ~NVSDK_NGX_DLSS_Feature_Flags_MVJittered) | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (current && (current->device!=device || current->rasterQueue!=rasterQueue || current->w!=active->width ||
        current->h!=active->height || current->ow!=od.Width || current->oh!=od.Height || current->flags!=flags ||
        current->colorFormat!=cd.Format || current->outputFormat!=od.Format || current->hasMotion!=(motion!=nullptr) ||
        current->hasExposure!=(exposure!=nullptr) || !SameTexture(current->depthDesc,dd) ||
        !SameTexture(current->motionDesc,md) || !SameTexture(current->exposureDesc,ed))) Cancel();
    if (!current)
    {
        if (retired.size()>=3) { device->Release(); Say("waiting for old GPU resources; raster is not blocked"); return; }
        current=std::make_unique<Context>(); auto& c=*current;
        c.device=device; c.rasterQueue=rasterQueue; rasterQueue->AddRef();
        c.w=active->width; c.h=active->height; c.ow=(unsigned)od.Width; c.oh=od.Height; c.flags=flags;
        c.colorFormat=cd.Format; c.outputFormat=od.Format; c.depthDesc=dd; c.motionDesc=md; c.exposureDesc=ed;
        c.hasMotion=motion!=nullptr; c.hasExposure=exposure!=nullptr;
        if (!Allocate(c)) { c.failed=true; Say("allocation/init failed; raster remains clean"); return; }
    }
    else device->Release();
    auto& c=*current;
    if (c.sawFrame && c.lastEpoch==epoch) { ++c.revision; c.latest=-1; Say("inactive: multiple upscales per frame"); return; }
    if (UInt(p,NVSDK_NGX_Parameter_Reset) || (c.sawFrame && epoch!=c.lastEpoch+1)) { ++c.revision; c.latest=-1; }
    c.sawFrame=true; c.lastEpoch=epoch;
    Pump(c);
    pending={cmd,p,output,epoch,std::max(Float(p,NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1),1e-4f)};
    if (c.failed) return;
    if (c.phase!=Context::Idle) { ++c.dropped; return; }
    int result=c.WritableResult(); if (result<0) { ++c.dropped; return; }
    int marker=c.ReserveMarker(); if (marker<0) { ++c.dropped; return; }
    c.writing=result; c.captureMarker=(unsigned)marker; c.capturedRevision=c.revision; c.capturedEpoch=epoch;
    auto copy=[&](ID3D12Resource* src,ID3D12Resource* dst,D3D12_RESOURCE_STATES arrival,bool crop)
    {
        Barrier(cmd,src,arrival,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(cmd,dst,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        if (crop) CopyActiveColor(cmd,dst,src,*active); else cmd->CopyResource(dst,src);
        Barrier(cmd,dst,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        Barrier(cmd,src,D3D12_RESOURCE_STATE_COPY_SOURCE,arrival);
    };
    copy(color,c.color,(D3D12_RESOURCE_STATES)cfg.ColorResourceBarrier.value_or(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),true);
    copy(depth,c.depth,(D3D12_RESOURCE_STATES)cfg.DepthResourceBarrier.value_or(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),false);
    if (motion) copy(motion,c.motion,(D3D12_RESOURCE_STATES)cfg.MVResourceBarrier.value_or(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),false);
    if (exposure) copy(exposure,c.exposure,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,false);
    auto* job=c.parameters;
    job->Set(NVSDK_NGX_Parameter_Color,c.color); job->Set(NVSDK_NGX_Parameter_Output,c.dummyOutput);
    job->Set(NVSDK_NGX_Parameter_Depth,c.depth); job->Set(NVSDK_NGX_Parameter_MotionVectors,c.motion);
    job->Set(NVSDK_NGX_Parameter_ExposureTexture,c.exposure);
    job->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,c.flags);
    job->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,c.w);
    job->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,c.h);
    job->Set(NVSDK_NGX_Parameter_PerfQualityValue,(int)UInt(p,NVSDK_NGX_Parameter_PerfQualityValue,NVSDK_NGX_PerfQuality_Value_MaxPerf));
    job->Set(NVSDK_NGX_Parameter_Reset,1u);
    for (const char* key:{NVSDK_NGX_Parameter_MV_Scale_X,NVSDK_NGX_Parameter_MV_Scale_Y,NVSDK_NGX_Parameter_DLSS_Pre_Exposure})
        job->Set(key,Float(p,key,1));
    for (const char* key:{NVSDK_NGX_Parameter_Jitter_Offset_X,NVSDK_NGX_Parameter_Jitter_Offset_Y}) job->Set(key,Float(p,key,0));
    job->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,Float(p,NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,16.67f));
    c.Mark(cmd,c.captureMarker); c.phase=Context::Capturing;
}

void After(ID3D12GraphicsCommandList* cmd,NVSDK_NGX_Parameter* p,UINT64 epoch)
{
    auto pair=pending; pending={};
    if (!current || pair.cmd!=cmd || pair.parameters!=p || pair.epoch!=epoch ||
        pair.output!=GetResource(p,NVSDK_NGX_Parameter_Output,"DLSSD.Output")) return;
    auto& c=*current;
    if (c.latest<0) { if (!c.failed) Say("warming up; current raster continues without NR"); return; }
    int marker=c.ReserveMarker(); if (marker<0) { Say("consumer slots busy; current raster retained"); return; }
    auto& cfg=*Config::Instance();
    ScopedNrStateEnvelope envelope(cmd);
    auto arrival=(D3D12_RESOURCE_STATES)cfg.OutputResourceBarrier.value_or(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(cmd,pair.output,arrival,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(cmd,c.clean,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(c.clean,pair.output);
    Barrier(cmd,c.clean,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto* residual=c.results[c.latest];
    Barrier(cmd,residual,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmd,c.composed,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DlssNrConstants apply {}; apply.Mode=DlssNrMode_ApplyResidual; apply.Width=c.ow; apply.Height=c.oh;
    apply.ExposurePreMul=pair.preExposure;
    bool ok=c.codec->DispatchPass(cmd,apply,c.clean,residual,nullptr,nullptr,nullptr,c.composed,nullptr);
    if (ok)
    {
        Barrier(cmd,c.composed,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(cmd,pair.output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(pair.output,c.composed);
        Barrier(cmd,pair.output,D3D12_RESOURCE_STATE_COPY_DEST,arrival);
        Barrier(cmd,c.composed,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        Say(c.failed ? "NR stopped after failure; CURRENT raster uses last completed residual" :
            "running: latest completed NR on CURRENT raster; no NR completion wait or residual FG");
    }
    else
    {
        Barrier(cmd,pair.output,D3D12_RESOURCE_STATE_COPY_SOURCE,arrival);
        Barrier(cmd,c.composed,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
        Say("composition failed; current raster retained");
    }
    Barrier(cmd,c.clean,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    Barrier(cmd,residual,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    c.resultUse[c.latest]=(unsigned)marker; c.resultUsed[c.latest]=true; c.Mark(cmd,(unsigned)marker);
}
std::string Status()
{
    auto s=status;
    if (current && current->latest>=0)
        s+=" (residual age "+std::to_string(current->lastEpoch-current->publishedEpoch)+" frames; completed "+
           std::to_string(current->published)+", requests dropped "+std::to_string(current->dropped)+")";
    return s;
}
}

std::string DeferredDlssStatus()
{
    std::lock_guard<std::recursive_mutex> lock(g_nrMutex);
    return Config::Instance()->DlssNrAsyncLatest.value_or_default() ? AsyncLatest::Status() : SynchronousDeferredDlssStatus();
}
