# SPDX-License-Identifier: BSD-3-Clause
"""External control of disposable macOS Stageviz benchmark processes."""
import hashlib
import json
import os
import re
from pathlib import Path
import socket
import subprocess
import threading
import time
import urllib.request

_MACOS_BOOTSTRAP = r'''import ctypes,stageviz,shiboken6
# Keep this finite user-requested benchmark from being background-throttled.
# NSActivityUserInitiatedAllowingIdleSystemSleep, from NSProcessInfo.h.
objc=ctypes.CDLL('/usr/lib/libobjc.A.dylib')
objc.objc_getClass.argtypes=[ctypes.c_char_p];objc.objc_getClass.restype=ctypes.c_void_p
objc.sel_registerName.argtypes=[ctypes.c_char_p];objc.sel_registerName.restype=ctypes.c_void_p
send=ctypes.CFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p)(('objc_msgSend',objc))
textSend=ctypes.CFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_char_p)(('objc_msgSend',objc))
activitySend=ctypes.CFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint64,ctypes.c_void_p)(('objc_msgSend',objc))
info=send(objc.objc_getClass(b'NSProcessInfo'),objc.sel_registerName(b'processInfo'))
reason=textSend(objc.objc_getClass(b'NSString'),objc.sel_registerName(b'stringWithUTF8String:'),b'Stageviz disposable interaction benchmark')
activity=activitySend(info,objc.sel_registerName(b'beginActivityWithOptions:reason:'),0x00EFFFFF,reason)
assert activity
objc.objc_retain.argtypes=[ctypes.c_void_p];objc.objc_retain.restype=ctypes.c_void_p
stageviz._scaleActivity=objc.objc_retain(activity)
def endActivity():
    if stageviz._scaleActivity:
        end=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p)(('objc_msgSend',objc))
        end(info,objc.sel_registerName(b'endActivity:'),stageviz._scaleActivity)
        objc.objc_release.argtypes=[ctypes.c_void_p];objc.objc_release.restype=None
        objc.objc_release(stageviz._scaleActivity)
        stageviz._scaleActivity=None
stageviz._scaleEndActivity=endActivity
from PySide6.QtCore import QObject
from PySide6.QtNetwork import QTcpServer,QHostAddress
class AppWrapper(ctypes.Structure):
    _fields_=[('refcount',ctypes.c_ssize_t),('type',ctypes.c_void_p),('application',ctypes.c_void_p)]
wrapper=stageviz.application()
accessor=ctypes.CDLL(None)._ZNK8stageviz11Application9mcpServerEv
accessor.argtypes=[ctypes.c_void_p];accessor.restype=ctypes.c_void_p
obj=shiboken6.wrapInstance(accessor(AppWrapper.from_address(id(wrapper)).application),QObject)
listeners=obj.findChildren(QTcpServer)
assert len(listeners)==1 and listeners[0].listen(QHostAddress(QHostAddress.SpecialAddress.LocalHost),__PORT__)
'''


def macos_bootstrap(port):
    """Reuse the isolated native MCP listener and finite macOS activity scope."""
    return _MACOS_BOOTSTRAP.replace('__PORT__', str(port))


class DisposableStageviz:
    def __init__(self, app, output, port=37657, environment=None, max_rss_mib=24576):
        self.app = Path(app).resolve()
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.port = port
        self.environment = environment or {}
        self.max_rss_mib = max_rss_mib
        self.stop = threading.Event()
        self.watchdog_error = None

    def rpc(self, method, params, timeout=900):
        request = urllib.request.Request('http://127.0.0.1:%d/mcp' % self.port,
            json.dumps(dict(jsonrpc='2.0', id=1, method=method, params=params)).encode(),
            {'Content-Type': 'application/json', 'Host': '127.0.0.1:37651'})
        with urllib.request.urlopen(request, timeout=timeout) as response:
            result = json.load(response)
        assert 'error' not in result, result
        return result

    def tool(self, name, arguments=None, timeout=900):
        result = self.rpc('tools/call', dict(name=name, arguments=arguments or {}), timeout)
        assert not result['result'].get('isError'), result
        return result

    def python(self, code, timeout=900):
        marker = 'STAGEVIZ_EXTERNAL_STEP_OK'
        result = self.tool('stageviz_run_python', {'code': code + '\nprint(%r)' % marker}, timeout)
        assert any(marker in block.get('text', '').splitlines() for block in result['result']['content']), result
        return result

    def oracle(self):
        return {name: self.tool(name) for name in ('stageviz_get_stage_info', 'stageviz_get_selection')}

    def _watch(self):
        pressure_at=0
        with (self.output / 'resources.jsonl').open('w') as stream:
            while not self.stop.wait(1):
                if self.process.poll() is not None:
                    return
                fields = subprocess.check_output(['ps', '-o', 'rss=,%cpu=', '-p', str(self.process.pid)], text=True).split()
                if len(fields) < 2:
                    continue
                rss = float(fields[0]) / 1024
                row=dict(wall_ms=time.time()*1000,rss_mib=rss,cpu_percent=float(fields[1]))
                if time.monotonic()-pressure_at>=10:
                    row['swap_usage']=subprocess.check_output(['sysctl','-n','vm.swapusage'],text=True).strip()
                    match=re.search(r'used = ([0-9.]+)([MGK])',row['swap_usage'])
                    if match:row['system_swap_used_mib']=float(match[1])*{'M':1,'G':1024,'K':1/1024}[match[2]]
                    row['vm_stat']=subprocess.check_output(['vm_stat'],text=True)
                    pressure_at=time.monotonic()
                stream.write(json.dumps(row)+'\n')
                stream.flush()
                if rss > self.max_rss_mib:
                    self.watchdog_error = 'RSS watchdog exceeded %.0f MiB' % rss
                    self.process.terminate()
                    return

    def __enter__(self):
        with socket.socket() as probe:
            assert probe.connect_ex(('127.0.0.1', self.port)) != 0, 'Dedicated MCP port is occupied'
        bootstrap = self.output / 'mcp-start.py'
        bootstrap.write_text(macos_bootstrap(self.port))
        env = dict(os.environ)
        env.update(self.environment)
        env['DYLD_IMAGE_SUFFIX'] = '_debug'
        self.log = (self.output / 'app.log').open('w')
        self.process = subprocess.Popen([str(self.app), '--execute', 'exec(open(%r).read())' % str(bootstrap)], env=env, stdout=self.log, stderr=subprocess.STDOUT)
        self.watcher = threading.Thread(target=self._watch, daemon=True)
        self.watcher.start()
        (self.output / 'provenance.json').write_text(json.dumps(dict(app=str(self.app), app_sha256=hashlib.sha256(self.app.read_bytes()).hexdigest(), pid=self.process.pid, environment=self.environment, activity='NSActivityUserInitiatedAllowingIdleSystemSleep', port=self.port), indent=2))
        deadline = time.monotonic() + 90
        try:
            while True:
                try:
                    tools = self.rpc('tools/list', {}, timeout=2)
                    (self.output / 'tools.json').write_text(json.dumps(tools, indent=2))
                    return self
                except Exception:
                    assert self.process.poll() is None and time.monotonic() < deadline, 'Disposable app failed to start'
                    time.sleep(.25)
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *args):
        outcome = dict(before_cleanup_returncode=self.process.poll(), watchdog_error=self.watchdog_error)
        if len(args) > 1 and args[1] is not None:
            outcome['exception'] = str(args[1])
        self.stop.set()
        self.process.terminate()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.watcher.join(timeout=3)
        self.log.close()
        outcome['cleanup_returncode'] = self.process.returncode
        (self.output / 'process-outcome.json').write_text(json.dumps(outcome, indent=2))
