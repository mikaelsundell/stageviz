# SPDX-License-Identifier: BSD-3-Clause
"""External control of disposable Stageviz test and benchmark processes."""
import hashlib
import json
import os
import re
from pathlib import Path
import socket
import sys
from urllib.parse import urlparse
import subprocess
import threading
import time
import urllib.request

_MACOS_BOOTSTRAP = r'''import ctypes,stageviz
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
'''


def macos_bootstrap(port):
    """Reuse the isolated native MCP listener and finite macOS activity scope."""
    return _MACOS_BOOTSTRAP + '\nimport os,stageviz\nos.environ[\"STAGEVIZ_TEST_MCP_PORT\"]=%r\nstageviz.startMcpServer()\n' % str(port)


def windows_rss(pid):
    """Return the owned Windows process's working set in MiB."""
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                'peak_working_set', 'working_set', 'peak_paged_pool', 'paged_pool',
                'peak_nonpaged_pool', 'nonpaged_pool', 'pagefile', 'peak_pagefile')]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return counters.working_set / (1024 * 1024)
    finally:
        kernel.CloseHandle(handle)


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
            {'Content-Type': 'application/json', 'Host': '127.0.0.1:%d' % self.port})
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
                try:
                    if sys.platform == 'win32':
                        rss, cpu = windows_rss(self.process.pid), None
                    else:
                        fields = subprocess.check_output(['ps', '-o', 'rss=,%cpu=', '-p', str(self.process.pid)], text=True).split()
                        if len(fields) < 2:
                            continue
                        rss, cpu = float(fields[0]) / 1024, float(fields[1])
                except Exception as error:
                    if self.process.poll() is None:
                        self.watchdog_error = 'Resource monitor failed: %s' % error
                        self.process.terminate()
                    return
                row=dict(wall_ms=time.time()*1000,rss_mib=rss,cpu_percent=cpu)
                if sys.platform == 'darwin' and time.monotonic()-pressure_at>=10:
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
        if self.port:
            with socket.socket() as probe:
                assert probe.connect_ex(('127.0.0.1', self.port)) != 0, 'Dedicated MCP port is occupied'
        bootstrap = self.output / 'mcp-start.py'
        # The native app isolates settings before constructing its UI.
        settings_directory = self.output / 'settings'
        settings_directory.mkdir()
        endpoint = self.output / 'endpoint.json'
        activity = _MACOS_BOOTSTRAP if sys.platform == 'darwin' else ''
        bootstrap.write_text(activity + '\nimport json,os,stageviz\nfrom pathlib import Path\n'
            + 'from PySide6.QtCore import QSettings\n'
            + 'test_settings=Path(os.environ["STAGEVIZ_TEST_SETTINGS_DIR"]).resolve()\n'
            + 'assert QSettings.defaultFormat()==QSettings.Format.IniFormat, "Rebuild Stageviz for isolated test settings"\n'
            + 'assert test_settings in Path(QSettings(QSettings.defaultFormat(),QSettings.Scope.UserScope,"StagevizTest","probe").fileName()).resolve().parents, "Test settings are not isolated"\n'
            + 'Path(%r).write_text(json.dumps({\"url\":stageviz.startMcpServer()}))\n' % str(endpoint), encoding='utf-8')
        env = dict(os.environ)
        env.update(self.environment)
        if sys.platform == 'darwin':
            env['DYLD_IMAGE_SUFFIX'] = '_debug'
        env['STAGEVIZ_TEST_MCP_PORT'] = str(self.port)
        env['STAGEVIZ_TEST_SETTINGS_DIR'] = str(settings_directory)
        self.log = (self.output / 'app.log').open('w')
        self.process = subprocess.Popen([str(self.app), '--execute', 'exec(open(%r, encoding="utf-8").read())' % str(bootstrap)], env=env, stdout=self.log, stderr=subprocess.STDOUT)
        self.watcher = threading.Thread(target=self._watch, daemon=True)
        self.watcher.start()
        (self.output / 'provenance.json').write_text(json.dumps(dict(app=str(self.app), app_sha256=hashlib.sha256(self.app.read_bytes()).hexdigest(), pid=self.process.pid, environment=self.environment, activity='NSActivityUserInitiatedAllowingIdleSystemSleep' if sys.platform == 'darwin' else None, port=self.port), indent=2))
        deadline = time.monotonic() + 90
        try:
            while True:
                try:
                    if not endpoint.is_file():
                        raise RuntimeError('Waiting for public MCP startup')
                    url = urlparse(json.loads(endpoint.read_text(encoding='utf-8'))['url'])
                    assert url.hostname == '127.0.0.1' and url.port, 'Unexpected disposable MCP endpoint'
                    self.port = url.port
                    tools = self.rpc('tools/list', {}, timeout=2)
                    (self.output / 'tools.json').write_text(json.dumps(tools, indent=2))
                    provenance = json.loads((self.output / 'provenance.json').read_text())
                    provenance['port'] = self.port
                    (self.output / 'provenance.json').write_text(json.dumps(provenance, indent=2))
                    return self
                except Exception:
                    assert self.process.poll() is None and time.monotonic() < deadline, 'Disposable app failed to start'
                    time.sleep(.25)
        except BaseException:
            self.__exit__(*sys.exc_info())
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
        if not args or args[0] is None:
            if self.watchdog_error:
                raise RuntimeError(self.watchdog_error)
            if outcome['before_cleanup_returncode'] is not None:
                raise RuntimeError('Disposable app exited before cleanup: %s' % outcome['before_cleanup_returncode'])
