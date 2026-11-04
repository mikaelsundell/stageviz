param([string]$Run = ('manual-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = 'Stop'
$appDir = 'C:\cadio-tools\Stageviz\Stageviz_0.4.19_261103_Release'
$resultsDir = 'C:\Temp\usdtest\storm-investigation'
$runDir = Join-Path $resultsDir $Run
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $resultsDir 'run01') -Filter 'expected*' -File | Copy-Item -Destination $runDir
$env:STORM_TRACE_DIR = $runDir
$env:STORM_TRACE_PRIM = '/Master/Body/Body_Position/Pillars/_120_8512_UPHOLSTERY_PANEL__SIDE/_120_8512_030_PANEL_A_PILLAR/DS0000662546/PANEL_ASSY_A_PILLAR_LH_UPR/PANEL_ASSY_A_PILLAR_LH_UPR/COVER_LH_A_PILLAR_DEFROSTER/lods/CAD8894839015_COVER_LH_A_PILLAR_DEFROSTER/__/__'
$env:STORM_TRACE_BYPASS_STAGING = '1'
Remove-Item Env:STORM_TRACE_UPLOAD_CHUNK_MB -ErrorAction SilentlyContinue
Remove-Item Env:HD_ENABLE_GPU_FRUSTUM_CULLING -ErrorAction SilentlyContinue
$env:HGIGL_DEBUG = '1'
$env:GLF_ENABLE_DIAGNOSTIC_TRACE = '1'
$env:TF_DEBUG = 'HGIGL_DEBUG_ERROR_STACKTRACE GLF_DEBUG_ERROR_STACKTRACE'
& (Join-Path $appDir 'Stageviz.exe') --open (Join-Path $resultsDir 'run01\visible-wrapper.usda') --execute 'import stageviz; stageviz.startMcpServer()' 1> (Join-Path $runDir 'stdout.log') 2> (Join-Path $runDir 'stderr.log')
