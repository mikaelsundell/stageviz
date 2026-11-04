"""External controller. The embedded benchmark never synchronously calls MCP."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import urllib.request

ROOT=Path(os.environ.get('STAGEVIZ_INVESTIGATION_DIR',r'C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test3-performance'))
APP=Path(r'C:\git\github\stageviz\deploy.Release\Stageviz.exe')
SCRIPT=Path(r'C:\git\github\stageviz\CTest\benchmark\benchmark_storm_layers.py')
LAYERS=[r'\\shared-fs2\clients\volvobuses\assets\received\261009_chassis_updates\B13R_4x2_6x2_Rear_I-pump\LHD\assembly\LHD_Front.usd',r'\\shared-fs2\clients\volvobuses\assets\received\261009_chassis_updates\B13R_4x2_6x2_Rear_I-pump\B13R 4x2 FWSI Rear Axle\assembly\B13R_4x2_Rear_Axle.usd']
LAYERS=[str(Path(path)).replace('\\','/') for path in LAYERS]

def rpc(method,params=None,timeout=30):
    request=urllib.request.Request('http://127.0.0.1:37651/mcp',json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params or {}}).encode(),{'Content-Type':'application/json'})
    with urllib.request.urlopen(request,timeout=timeout) as response:return json.load(response)

def tool(name,args=None):
    result=rpc('tools/call',{'name':name,'arguments':args or {}})
    assert 'error' not in result,result
    content=result.get('result',{}).get('content',[])
    if any(block.get('text','').startswith("<class '") for block in content):raise RuntimeError(result)
    return result

class Memory(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[(key,ctypes.c_size_t) for key in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]

def usage(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.restype=wintypes.HANDLE
    handle=kernel.OpenProcess(0x410,False,pid)
    if not handle:return {}
    try:
        mem=Memory();mem.cb=ctypes.sizeof(mem)
        ctypes.WinDLL('psapi').GetProcessMemoryInfo(handle,ctypes.byref(mem),mem.cb)
        stamps=[wintypes.FILETIME() for _ in range(4)]
        kernel.GetProcessTimes(handle,*[ctypes.byref(stamp) for stamp in stamps])
        ticks=lambda stamp:(stamp.dwHighDateTime<<32)|stamp.dwLowDateTime
        return {'rss_mb':mem.WorkingSetSize/1048576,'private_mb':mem.PrivateUsage/1048576,'cpu_seconds':(ticks(stamps[2])+ticks(stamps[3]))/1e7}
    finally:kernel.CloseHandle(handle)

def run_profile(mb,index,args,profile=None):
    profile=profile or {}
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    label=f'{stamp}-{index:02d}-{mb}MiB'+('-trace' if args.trace else '')+('-materials-off' if args.no_materials else '')+('-smoke' if args.smoke else '')
    if profile.get('name'):label+='-'+''.join(c for c in profile['name'] if c.isalnum() or c in '-_')
    directory=Path(r'C:\Temp\usdtest\performance')/label
    directory.mkdir(parents=True)
    config={'output':str(directory),'layers':LAYERS,'materials':not args.no_materials,'layerEditor':not args.closed_editor,'stagingMiB':mb,'operationTimeoutSeconds':1800,'traceOperation':'mute_front_1' if args.trace else None,'smoke':args.smoke}
    config.update(profile.get('view',{}))
    (directory/'config.json').write_text(json.dumps(config,indent=2))
    env=os.environ.copy()
    for key in list(env):
        if key.startswith('STORM_TRACE_') or key in ('TF_DEBUG','HGIGL_DEBUG','GLF_ENABLE_DIAGNOSTIC_TRACE','HD_ENABLE_GPU_FRUSTUM_CULLING'):env.pop(key,None)
    env.update(HDST_MAX_STAGING_BUFFER_SIZE=str(mb*1048576),STAGEVIZ_PERF_LOG=str(directory/'native-phases.jsonl'),STAGEVIZ_STORM_PROFILE=str(directory/'config.json'))
    env.update({str(k):str(v) for k,v in profile.get('env',{}).items()})
    if args.storm_log:env['HDST_PERF_LOG']=str(directory/'storm-phases.jsonl')
    stdout=open(directory/'stdout.log','wb');stderr=open(directory/'stderr.log','wb')
    process=subprocess.Popen([str(APP),'--execute','import stageviz; stageviz.startMcpServer()'],cwd=APP.parent,env=env,stdout=stdout,stderr=stderr)
    provenance={'pid':process.pid,'exe':str(APP),'exeSHA256':hashlib.sha256(APP.read_bytes()).hexdigest(),'env':{key:value for key,value in env.items() if key.startswith(('STAGEVIZ_','HDST_','HD_ENABLE_'))},'cachePolicy':'Fresh process; OS/network/shader caches not cleared','config':config}
    provenance.update(scriptSHA256=hashlib.sha256(SCRIPT.read_bytes()).hexdigest(),python=sys.version,cpu=os.environ.get('PROCESSOR_IDENTIFIER'),logicalCPUs=os.cpu_count(),datasets=[{'path':path,'bytes':Path(path).stat().st_size,'mtime_ns':Path(path).stat().st_mtime_ns} for path in config['layers']])
    shutil.copy2(SCRIPT,directory/'benchmark-used.py')
    shutil.copy2(Path(__file__),directory/'controller-used.py')
    (directory/'launch.json').write_text(json.dumps(provenance,indent=2))
    print('LAUNCH',label,'pid',process.pid,flush=True)
    try:
        for _ in range(120):
            if process.poll() is not None:raise RuntimeError('Stageviz exited during startup')
            try:
                tools=rpc('tools/list',timeout=2)
                break
            except Exception:time.sleep(.5)
        else:raise TimeoutError('MCP startup')
        (directory/'tools.json').write_text(json.dumps(tools,indent=2))
        assert hashlib.sha256(SCRIPT.read_bytes()).hexdigest()==provenance['scriptSHA256'],'Benchmark source changed during startup'
        result=tool('stageviz_run_python',{'code':f'import runpy; runpy.run_path({str(SCRIPT)!r})'})
        (directory/'bootstrap.json').write_text(json.dumps(result,indent=2))
        last_sample=last_progress=0
        last_status=None
        acknowledged=set()
        while True:
            now=time.monotonic()
            if process.poll() is not None:raise RuntimeError('Stageviz exited: '+str(process.returncode))
            path=directory/'status.json'
            try:status=json.loads(path.read_text()) if path.exists() else {}
            except (json.JSONDecodeError,PermissionError):status={}
            state=(status.get('state'),status.get('phase'))
            if state!=last_status or now-last_progress>30:
                print('PROGRESS',label,status.get('state'),status.get('phase'),json.dumps(usage(process.pid)),flush=True)
                last_status=state;last_progress=now
            if now-last_sample>10:
                sample={'wall_ms':time.time()*1000,'monotonic_ms':time.perf_counter()*1000,**usage(process.pid)}
                try:sample['gpu']=subprocess.check_output(['nvidia-smi','--query-gpu=memory.used,utilization.gpu','--format=csv,noheader,nounits'],text=True).strip()
                except Exception:pass
                with (directory/'resources.jsonl').open('a') as output:output.write(json.dumps(sample)+'\n')
                last_sample=now
            if status.get('state')=='failed':raise RuntimeError(status.get('error'))
            if status.get('state')=='running' and (time.time()*1000-status.get('start_wall_ms',time.time()*1000))>1800000:
                raise TimeoutError('External operation watchdog: '+str(status.get('phase')))
            if status.get('state')=='checkpoint' and status['phase'] not in acknowledged:
                phase=status['phase']
                oracle={name:tool(name,arguments) for name,arguments in [('stageviz_get_stage_info',{}),('stageviz_list_prims',{'path':'/','maxDepth':1,'limit':100}),('stageviz_get_selection',{})]}
                (directory/(phase+'-oracle.json')).write_text(json.dumps(oracle,indent=2))
                print('RESULT',label,json.dumps({key:status['result'][key] for key in ('phase','dispatch_ms','end_to_end_ms','prims','muted')}),flush=True)
                (directory/'ack.txt').write_text(phase)
                acknowledged.add(phase)
            if status.get('state')=='complete':break
            time.sleep(.25)
        return directory
    except BaseException as error:
        (directory/'controller-error.json').write_text(json.dumps({'type':type(error).__name__,'message':str(error),'wall_ms':time.time()*1000},indent=2))
        raise
    finally:
        shutdown_started=time.time()
        terminated=False
        if process.poll() is None:
            try:tool('stageviz_run_python',{'code':'from PySide6.QtCore import QTimer; from PySide6.QtWidgets import QApplication; stageviz.session().close(); QTimer.singleShot(0, QApplication.quit)'})
            except Exception as error:
                (directory/'shutdown-note.txt').write_text(str(error))
            try:process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                terminated=True
                process.terminate()
                process.wait(timeout=30)
        (directory/'shutdown.json').write_text(json.dumps({'exitCode':process.returncode,'terminatedByController':terminated,'elapsedSeconds':time.time()-shutdown_started},indent=2))
        stdout.close();stderr.close()
        shutil.copytree(directory,ROOT/'runs'/label,dirs_exist_ok=True)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--profiles',default='64,16,256,64')
    p.add_argument('--trace',action='store_true')
    p.add_argument('--storm-log',action='store_true')
    p.add_argument('--no-materials',action='store_true')
    p.add_argument('--closed-editor',action='store_true')
    p.add_argument('--smoke',action='store_true')
    p.add_argument('--settings')
    p.add_argument('--select',help='Comma-separated profile names from --settings')
    args=p.parse_args()
    try:rpc('ping',timeout=1)
    except Exception:pass
    else:raise RuntimeError('An existing MCP server occupies the port; preserve that session and close it before running')
    profiles=json.loads(Path(args.settings).read_text())['profiles'] if args.settings else [{'stagingMiB':mb} for mb in map(int,args.profiles.split(','))]
    if args.select:
        names=set(args.select.split(','))
        profiles=[profile for profile in profiles if profile.get('name') in names]
        assert {profile['name'] for profile in profiles}==names,'Unknown selected profile'
    for index,profile in enumerate(profiles):
        for layer in profile.get('view',{}).get('layers',LAYERS):assert Path(layer).is_file(),layer
        run_profile(int(profile.get('stagingMiB',64)),index,args,profile)

if __name__=='__main__':main()
