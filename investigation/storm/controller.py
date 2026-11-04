"""External MCP controller; never run this inside Stageviz's Qt thread."""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

BASE = Path(r'C:\Temp\usdtest\storm-investigation')
APP = Path(r'C:\cadio-tools\Stageviz\Stageviz_0.4.19_261103_Release')
USD = Path(r'C:\git\3rdparty\packages\OpenUSD-25.11')
TARGET = '/Master/Body/Body_Position/Pillars/_120_8512_UPHOLSTERY_PANEL__SIDE/_120_8512_030_PANEL_A_PILLAR/DS0000662546/PANEL_ASSY_A_PILLAR_LH_UPR/PANEL_ASSY_A_PILLAR_LH_UPR/COVER_LH_A_PILLAR_DEFROSTER/lods/CAD8894839015_COVER_LH_A_PILLAR_DEFROSTER/__/__'

def rpc(method, params=None):
    data = json.dumps(dict(jsonrpc='2.0', id=1, method=method, params=params or {})).encode()
    request = urllib.request.Request('http://127.0.0.1:37651/mcp', data, {'Content-Type':'application/json'})
    with urllib.request.urlopen(request, timeout=300) as response:
        return json.load(response)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['deploy','launch','list','info','prim','python','quit'])
    p.add_argument('--run', default='run01')
    p.add_argument('--no-cull', action='store_true')
    p.add_argument('--bypass-staging', action='store_true')
    p.add_argument('--chunk-mb', type=int, default=0)
    p.add_argument('--production', action='store_true')
    p.add_argument('--code-file')
    p.add_argument('--label', default='frame-all')
    p.add_argument('--stage', default=r'C:\Temp\usdtest\TEMP_asm.usd')
    args = p.parse_args()
    BASE.mkdir(parents=True, exist_ok=True)
    run = BASE / args.run
    run.mkdir(exist_ok=True)
    if args.action == 'deploy':
        backup = BASE/'original-dlls'
        backup.mkdir(exist_ok=True)
        data=[]
        for lib in ('hdSt','hgiGL'):
            src = USD/('build/vc143/x86_amd64/Release/pxr/imaging/'+lib+'/Release/usd_'+lib+'.dll')
            dst = APP/('usd_'+lib+'.dll')
            if not (backup/dst.name).exists():
                shutil.copy2(dst, backup/dst.name)
            shutil.copy2(src, dst)
            data.append({'deployed': str(dst), 'sha256': hashlib.sha256(dst.read_bytes()).hexdigest(), 'originalSha256': hashlib.sha256((backup/dst.name).read_bytes()).hexdigest()})
    elif args.action == 'launch':
        for path in (BASE/'run01').glob('expected*'):
            if path.parent!=run:
                shutil.copy2(path, run/path.name)
        env = os.environ.copy()
        env.update(STORM_TRACE_PRIM=TARGET, STORM_TRACE_DIR=str(run), HGIGL_DEBUG='1', GLF_ENABLE_DIAGNOSTIC_TRACE='1', TF_DEBUG='HGIGL_DEBUG_ERROR_STACKTRACE GLF_DEBUG_ERROR_STACKTRACE')
        if args.bypass_staging:
            env['STORM_TRACE_BYPASS_STAGING']='1'
        else:
            env.pop('STORM_TRACE_BYPASS_STAGING',None)
        if args.chunk_mb:
            env['STORM_TRACE_UPLOAD_CHUNK_MB']=str(args.chunk_mb)
        else:
            env.pop('STORM_TRACE_UPLOAD_CHUNK_MB',None)
        if args.no_cull:
            env['HD_ENABLE_GPU_FRUSTUM_CULLING']='0'
        else:
            env.pop('HD_ENABLE_GPU_FRUSTUM_CULLING', None)
        if args.production:
            for key in ('HGIGL_DEBUG','GLF_ENABLE_DIAGNOSTIC_TRACE','TF_DEBUG','HDST_MAX_STAGING_BUFFER_SIZE'):
                env.pop(key,None)
        stdout = open(run/'stdout.log', 'wb')
        stderr = open(run/'stderr.log', 'wb')
        process = subprocess.Popen([str(APP/'Stageviz.exe'), '--execute', 'import stageviz; stageviz.startMcpServer()'], cwd=APP, env=env, stdout=stdout, stderr=stderr)
        data = {'pid': process.pid, 'env': {k:v for k,v in env.items() if k.startswith(('STORM_', 'HD_', 'HGI', 'GLF_', 'TF_DEBUG'))}}
    elif args.action == 'list':
        data = rpc('tools/list')
    elif args.action in ('info','prim'):
        data = rpc('tools/call', {'name':'stageviz_get_stage_info' if args.action=='info' else 'stageviz_get_prim_info', 'arguments':{} if args.action=='info' else {'path':TARGET}})
    else:
        code = ('import os; os.environ["STORM_CAPTURE_LABEL"]='+repr(args.label)+'\n'+Path(args.code_file).read_text()) if args.action=='python' else 'from PySide6.QtCore import QTimer; from PySide6.QtWidgets import QApplication; QTimer.singleShot(0, QApplication.quit)'
        try:
            data = rpc('tools/call', {'name':'stageviz_run_python', 'arguments':{'code':code}})
        except http.client.RemoteDisconnected:
            if args.action!='quit':raise
            data={'quitRequested':True, 'connectionClosed':True}
    (run/(args.action+'.json')).write_text(json.dumps(data, indent=2))
    summary = json.dumps(data, indent=2)
    print(summary if len(summary)<12000 else summary[:1000]+'\n[Full response saved to disk]')

if __name__ == '__main__':
    main()
