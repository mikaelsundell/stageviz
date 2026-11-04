from pathlib import Path
import shutil

usd=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
here=Path(__file__).resolve().parent
def edit(relative, transform):
    path=usd/relative
    original=path.read_text(encoding='utf-8')
    updated=transform(original)
    assert updated!=original,relative
    with path.open('w',encoding='utf-8',newline='') as output:output.write(updated)
def replace(source,old,new):
    assert source.count(old)==1,(old,source.count(old))
    return source.replace(old,new)
def scope(source,signature,metric,flush=False):
    start=source.index(signature);brace=source.index('{',start)
    return source[:brace+1]+f'\n    HdSt_Performance::Scope perfScope(HdSt_Performance::{metric}'+(', true' if flush else '')+');'+source[brace+1:]
def include(source):
    offset=source.index('#include ')
    return source[:offset]+'#include "pxr/imaging/hdSt/performance.h"\n'+source[offset:]

edit('pxr/imaging/hd/instanceRegistry.h',lambda s:replace(replace(s,'#include <memory>','#include <memory>\n#include <chrono>\n#include <utility>'),'    HdInstanceRegistry() = default;',(here/'incremental_registry.txt').read_text()+'    HdInstanceRegistry() = default;'))
for name in ('performance.h','performance.cpp'):shutil.copy2(here/name,usd/'pxr/imaging/hdSt'/name)
edit('pxr/imaging/hdSt/CMakeLists.txt',lambda s:replace(s,'    PRIVATE_CLASSES\n','    PRIVATE_CLASSES\n        performance\n'))

def registry_header(s):
    return replace(s,'protected:\n    void _Commit() override;', '''    /// Requests a new topology sweep after any active partial sweep.
    HDST_API
    void RequestTopologyGarbageCollection();

    /// True while opt-in topology reclamation needs another render frame.
    HDST_API
    bool HasPendingTopologyGarbageCollection() const;

protected:
    void _Commit() override;''')
edit('pxr/imaging/hdSt/resourceRegistry.h',registry_header)

def registry(s):
    s=include(s)
    pos=s.index('TF_DEFINE_PRIVATE_TOKENS(')
    settings='''TF_DEFINE_ENV_SETTING(HDST_TOPOLOGY_GC_BUDGET_US, 0,
    "Soft per-frame topology collection budget in microseconds; zero is eager");
TF_DEFINE_ENV_SETTING(HDST_TOPOLOGY_GC_MAX_VISITS, 8192,
    "Maximum topology entries visited per maintenance frame");
TF_DEFINE_ENV_SETTING(HDST_TOPOLOGY_GC_MAX_ENTRIES, 1000000,
    "Registry entry pressure threshold; above this use eager reclamation");

namespace {
// Bookkeeping uses the registry's existing extension point. No public class
// layout changes or process-global registry-pointer maps are needed.
class _TopologyGCState final : public HdResourceRegistry {
public:
    HdInstanceRegistry<HdSt_MeshTopologySharedPtr>::GarbageCollectionCursor cursor;
    bool forceFull = false;
};
constexpr const char* _topologyStateKey = "__stTopoGC";
int _TopologyBudget() {
    static const int value = std::max(0, std::min(1000000,
        TfGetEnvSetting(HDST_TOPOLOGY_GC_BUDGET_US)));
    return value;
}
}

'''
    s=s[:pos]+settings+s[pos:]
    s=replace(s,'HdStResourceRegistry::~HdStResourceRegistry()\n{','''HdStResourceRegistry::~HdStResourceRegistry()
{
    // Drain before Hgi collection and member destruction, preserving original
    // dependency ordering even if the application closes during maintenance.
    auto stateIt = _subResourceRegistries.find(_topologyStateKey);
    if (stateIt != _subResourceRegistries.end()) {
        auto *state = static_cast<_TopologyGCState*>(stateIt->second.get());
        state->forceFull = true;
        state->cursor.RequestRescan();
    }''')
    pos=s.index('void HdStResourceRegistry::InvalidateShaderRegistry()')
    methods='''void
HdStResourceRegistry::RequestTopologyGarbageCollection()
{
    if (!_TopologyBudget() && !HdSt_Performance::Enabled()) return;
    auto &entry = _subResourceRegistries[_topologyStateKey];
    if (!entry) entry = std::make_unique<_TopologyGCState>();
    static_cast<_TopologyGCState*>(entry.get())->cursor.RequestRescan();
}

bool
HdStResourceRegistry::HasPendingTopologyGarbageCollection() const
{
    auto it = _subResourceRegistries.find(_topologyStateKey);
    return it != _subResourceRegistries.end() &&
        static_cast<_TopologyGCState*>(it->second.get())->cursor.active;
}

'''
    s=s[:pos]+methods+s[pos:]
    s=scope(s,'HdStResourceRegistry::_GarbageCollect()','ResourceGC')
    s=replace(s,'        size_t count = _meshTopologyRegistry.GarbageCollect();','''        size_t count;
        if (_TopologyBudget() || HdSt_Performance::Enabled()) {
            auto &entry = _subResourceRegistries[_topologyStateKey];
            if (!entry) entry = std::make_unique<_TopologyGCState>();
            auto *state = static_cast<_TopologyGCState*>(entry.get());
            const size_t pressureLimit = static_cast<size_t>(std::max(1,
                TfGetEnvSetting(HDST_TOPOLOGY_GC_MAX_ENTRIES)));
            const bool forced = state->forceFull ||
                _meshTopologyRegistry.size() > pressureLimit;
            const int budget = forced ? 0 : _TopologyBudget();
            const size_t visits = budget ? static_cast<size_t>(std::max(1,
                TfGetEnvSetting(HDST_TOPOLOGY_GC_MAX_VISITS))) : 0;
            const auto step = _meshTopologyRegistry.GarbageCollectStep(
                &state->cursor, visits, std::chrono::microseconds(budget));
            count = step.remaining;
            HdSt_Performance::TopologyStep(this, step.visited, step.reclaimed,
                step.remaining, step.pending, forced, step.eraseNs,
                step.destroyNs, step.elapsedNs);
        } else {
            count = _meshTopologyRegistry.GarbageCollect();
        }''')
    return s
edit('pxr/imaging/hdSt/resourceRegistry.cpp',registry)

def delegate(s):
    s=include(s);s=scope(s,'HdStRenderDelegate::CommitResources(','Commit')
    return replace(s,'''    if (stRenderParam->IsGarbageCollectionNeeded()) {
        _resourceRegistry->GarbageCollect();''','''    const bool requested = stRenderParam->IsGarbageCollectionNeeded();
    if (requested) _resourceRegistry->RequestTopologyGarbageCollection();
    if (requested || _resourceRegistry->HasPendingTopologyGarbageCollection()) {
        _resourceRegistry->GarbageCollect();''')
edit('pxr/imaging/hdSt/renderDelegate.cpp',delegate)
edit('pxr/imaging/hdSt/renderPass.h',lambda s:replace(s,'protected:\n', '''    /// Maintenance frames keep bounded reclamation progressing while idle.
    HDST_API
    bool IsConverged() const override;

protected:
'''))
def renderpass(s):
    s=include(s)
    pos=s.index('HdSt_RenderPass::HdSt_RenderPass(')
    s=s[:pos]+'''bool
HdSt_RenderPass::IsConverged() const
{
    return !static_cast<HdStRenderDelegate*>(
        GetRenderIndex()->GetRenderDelegate())->GetResourceRegistry()
            ->HasPendingTopologyGarbageCollection();
}

'''+s[pos:]
    # The delegate getter returns the base registry pointer; use a typed cast.
    s=s.replace('return !static_cast<HdStRenderDelegate*>(\n        GetRenderIndex()->GetRenderDelegate())->GetResourceRegistry()\n            ->HasPendingTopologyGarbageCollection();','''return !std::static_pointer_cast<HdStResourceRegistry>(
        GetRenderIndex()->GetRenderDelegate()->GetResourceRegistry())
            ->HasPendingTopologyGarbageCollection();''')
    return scope(s,'HdSt_RenderPass::_Execute(','RenderPass',True)
edit('pxr/imaging/hdSt/renderPass.cpp',renderpass)

def drawing(s):
    s=include(s);s=scope(s,'HdSt_DrawBatch::_DrawingProgram::CompileShader(','ShaderPrepare')
    begin=s.index('    _resourceBinder.ResolveBindings(drawItem,')
    end=s.index('GetCapabilities());',begin)+len('GetCapabilities());')
    s=s[:begin]+'    { HdSt_Performance::Scope bindingScope(HdSt_Performance::Bindings);\n'+s[begin:end]+'\n    }'+s[end:]
    s=replace(s,'    HdSt_CodeGen codeGen(_geometricShader, shaders,','''    const auto setupBegin = HdSt_Performance::Enabled() ? HdSt_Performance::Now() : 0;
    HdSt_Performance::DestructionTimer destruction(HdSt_Performance::CodegenDestroy);
    HdSt_CodeGen codeGen(_geometricShader, shaders,''')
    s=replace(s,'    HdStGLSLProgram::ID hash = codeGen.ComputeHash();','''    HdSt_Performance::Arm armDestruction(destruction);
    if (setupBegin) HdSt_Performance::Add(HdSt_Performance::CodegenSetup,
        1, HdSt_Performance::Now()-setupBegin);
    HdStGLSLProgram::ID hash;
    { HdSt_Performance::Scope hashScope(HdSt_Performance::CodegenHash);
      hash = codeGen.ComputeHash(); }''')
    s=replace(s,'        if (programInstance.IsFirstInstance()) {','''        if (programInstance.IsFirstInstance()) {
            HdSt_Performance::Add(HdSt_Performance::ProgramMiss,1);
            HdSt_Performance::Scope compileScope(HdSt_Performance::CompileLink);''')
    needle='''        } else {
            if (TfDebug::IsEnabled(HDST_LOG_DRAWING_SHADER_PROGRAM_HITS)'''
    s=replace(s,needle,'''        } else {
            HdSt_Performance::Add(HdSt_Performance::ProgramHit,1);
            if (TfDebug::IsEnabled(HDST_LOG_DRAWING_SHADER_PROGRAM_HITS)''')
    return s
edit('pxr/imaging/hdSt/drawBatch.cpp',drawing)
def batches(s):
    s=include(s)
    s=scope(s,'HdStCommandBuffer::RebuildDrawBatchesIfNeeded(','BatchValidate')
    s=scope(s,'HdStCommandBuffer::_RebuildDrawBatches(','BatchRebuildAll')
    s=replace(s,'                    if (!_drawBatches[i]->Rebuild()) {','''                    HdSt_Performance::Scope rebuildScope(HdSt_Performance::BatchRebuild);
                    if (!_drawBatches[i]->Rebuild()) {''')
    s=replace(s,'    TF_DEBUG(HDST_DRAW_BATCH).Msg(\n        "   %lu draw batches created', '''    HdSt_Performance::Add(HdSt_Performance::DrawBatches,_drawBatches.size());
    HdSt_Performance::Add(HdSt_Performance::DrawItems,drawItems.size());
    TF_DEBUG(HDST_DRAW_BATCH).Msg(
        "   %lu draw batches created''')
    return s
edit('pxr/imaging/hdSt/commandBuffer.cpp',batches)
print('Patched opt-in Storm metrics and bounded topology collection')
