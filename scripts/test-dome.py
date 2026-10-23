#!/usr/bin/env python3
"""Native Wayland FLTK integration through DomeD desktop and XR menu input.
Same uv dependencies as test-ui.py. DOME_COMPOSITOR may select a build.
"""
import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from Xlib import X
from Xlib.ext import xtest
from PIL import Image, ImageStat

signal.pthread_sigmask(signal.SIG_SETMASK, [])
if os.environ.get('MIRO_TEST_BUS') != '1':
    os.execvpe('dbus-run-session', ['dbus-run-session', '--', sys.executable, *sys.argv],
              {**os.environ, 'MIRO_TEST_BUS': '1'})
spec = importlib.util.spec_from_file_location('ui', Path(__file__).with_name('test-ui.py'))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)
ui.out = ui.root / 'artifacts/dome'
ui.out.mkdir(parents=True, exist_ok=True)
dome_root = ui.root.parent / 'dome-compositor'
compositor = Path(os.environ.get('DOME_COMPOSITOR', dome_root / 'target/release/dome-compositor'))
stop = threading.Event()
comp = None

try:
    with tempfile.TemporaryDirectory(prefix='miro-dome-test-', ignore_cleanup_errors=True) as directory:
        temporary = Path(directory)
        temporary.chmod(0o700)
        home = temporary / 'home'
        config = home / '.config'
        (config / 'dome').mkdir(parents=True)
        (config / 'miro-pdf').mkdir()
        (config / 'dome/appearance.json').write_text(json.dumps({'font': 'Unifont / Sans-Serif', 'font_size': 16, 'font_scale': 1, 'light': False, 'accent': '#6babff'}))
        (config / 'dome/keyboard.json').write_text(json.dumps({'ime': False, 'layout': 'us'}))
        (config / 'miro-pdf/miro.conf').write_text('Bind x Normal NextPage\n')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        rd, wr = os.pipe()
        ui.start(['Xvfb', '-displayfd', str(wr), '-screen', '0', '1600x1000x24', '-nolisten', 'tcp'], 'xvfb', os.environ.copy(), pass_fds=(wr,))
        os.close(wr)
        with os.fdopen(rd) as pipe:
            ds = ':' + pipe.readline().strip()
        env = {**os.environ, 'HOME': str(home), 'DISPLAY': ds, 'XDG_RUNTIME_DIR': directory,
               'XDG_CONFIG_HOME': str(config), 'XDG_CACHE_HOME': str(home / '.cache'),
               'XDG_DATA_HOME': str(home / '.local/share'), 'XDG_STATE_HOME': str(home / '.local/state'),
               'XDG_CURRENT_DESKTOP': 'Dome', 'XDG_SESSION_DESKTOP': 'dome', 'XDG_SESSION_TYPE': 'wayland',
               'DOME_LOGIN_SESSION': '0', 'DOME_APPEARANCE_SERVICE': '1', 'DOME_START_XR': '0', 'DOME_START_IME': '0',
               'DOME_SESSION_RESTORE': '0', 'DOME_BACKEND': 'nested', 'DOME_RENDERER': 'gles',
               'DOME_PREVIEW_PORT': str(port), 'LIBGL_ALWAYS_SOFTWARE': '1', 'GIO_USE_VFS': 'local', 'NO_AT_BRIDGE': '1'}
        for name in ['WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'FONTCONFIG_FILE']:
            env.pop(name, None)
        subprocess.run(['dbus-update-activation-environment', 'HOME', 'DISPLAY', 'XDG_RUNTIME_DIR', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CURRENT_DESKTOP'], env=env, check=True)
        comp = ui.start([compositor, '--backend', 'nested'], 'compositor', env, start_new_session=True)
        def get(path):
            with urllib.request.urlopen(base + path, timeout=4) as response:
                return json.load(response)
        def post(path, value):
            request = urllib.request.Request(base + path, data=json.dumps(value).encode(), headers={'Content-Type': 'application/json'})
            urllib.request.urlopen(request, timeout=4).close()
        ui.wait(lambda: get('/api/state')['scene']['source'] == 'wayland')
        wayland = ui.wait(lambda: next((p for p in temporary.glob('wayland-*') if p.is_socket()), None))
        report_file = ui.out / 'wayland.json'
        report_file.unlink(missing_ok=True)
        viewer = ui.start([ui.binary, ui.root / 'assets/coordinate_points.pdf', '--report', report_file], 'viewer',
                          {**env, 'DISPLAY': '', 'WAYLAND_DISPLAY': wayland.name})
        def report():
            assert viewer.poll() is None, (ui.out / 'viewer.log').read_text()
            return json.loads(report_file.read_text())
        def ready():
            state = report()
            return state if state['tabs'] and state['tabs'][0]['ready'] and state['uploaded_tiles'] and state['menu_attached'] else None
        state = ui.wait(ready)
        window = ui.wait(lambda: next((w for w in get('/api/state')['scene']['windows'] if w['app_id'] == 'miro-pdf'), None))
        ui.check(state['font_file'].lower().endswith('.bdf'), 'Wayland FLTK uses the raw session BDF')
        ui.check(state['menu_attached'], 'FLTK associates its DBusMenu with its native Wayland surface')
        xd = ui.display.Display(ds)
        host = ui.wait(lambda: next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name() or '').startswith('Dome')), None))
        host.set_input_focus(X.RevertToParent, X.CurrentTime); xd.sync()
        def desktop_click(x, y):
            g = host.get_geometry()
            xtest.fake_input(xd, X.MotionNotify, x=int(g.x+x), y=int(g.y+y))
            for kind in [X.ButtonPress, X.ButtonRelease]:
                xtest.fake_input(xd, kind, 1); xd.sync(); time.sleep(.12)
        def select_dark(click):
            model = ui.menu(report())
            view = next(v.value for v in model[2] if v.value[1].get('label') and v.value[1]['label'].value.replace('_', '') == 'View')
            rows = [v.value for v in view[2]]
            index = next(i for i, n in enumerate(rows) if n[1].get('label') and n[1]['label'].value.replace('_', '') == 'Dark Document')
            click(360, 18); time.sleep(.3); click(350, 40+28*index+14)
        select_dark(desktop_click)
        ui.wait(lambda: report()['tabs'][0]['dark'] is False)
        ui.check(True, 'DomeD desktop universal menu invokes the native PDF rendering action')
        def frame(name):
            with urllib.request.urlopen(base + f'/api/xr-frame/{window["id"]}', timeout=5) as response:
                image = Image.frombytes('RGBA', (int(response.headers['X-Dome-Width']), int(response.headers['X-Dome-Height'])), response.read())
                image.save(ui.out / name); return image
        image = ui.wait(lambda: frame('desktop-light-pdf.png'))
        mean = sum(ImageStat.Stat(image.crop((image.width*.4, image.height*.4, image.width*.6, image.height*.6))).mean[:3])/3
        ui.check(mean > 180, 'Native Wayland document textures reach the compositor as visible PDF pixels')
        def heartbeat():
            while not stop.wait(.15):
                try: post('/api/xr-session', {'visible': True, 'focused': True})
                except OSError: pass
        post('/api/xr-session', {'visible': True, 'focused': True})
        threading.Thread(target=heartbeat, daemon=True).start()
        ui.wait(lambda: get('/api/xr-control')['input_realm'] == 'xr')
        post('/api/action', {'op': 'window_visibility', 'id': window['id'], 'desktop': True, 'xr': True})
        def focus_xr():
            post('/api/xr-input', {'events': [], 'actions': [{'op': 'focus', 'id': window['id']}]})
            return get('/api/state')['scene']['focused'] == window['id']
        ui.wait(focus_xr)
        def xr_click(x, y):
            layout = get('/api/xr-shell')
            post('/api/xr-input', {'events': [{'kind': 'motion', 'id': 0, 'uv': [x/layout['width'], y/layout['top']]}], 'actions': []})
            time.sleep(.15)
            post('/api/xr-input', {'events': [{'kind': 'button', 'button': 272, 'pressed': True}, {'kind': 'button', 'button': 272, 'pressed': False}], 'actions': []})
            time.sleep(.25)
        select_dark(xr_click)
        ui.wait(lambda: report()['tabs'][0]['dark'] is True)
        ui.check(True, 'XR universal menu invokes the same focused native PDF command')
        ui.wait(lambda: frame('xr-dark-pdf.png'))
        page = report()['tabs'][0]['page']
        ui.key(xd, host, 'x', ('Super_L',))
        ui.wait(lambda: get('/api/xr-control')['input_realm'] == 'desktop')
        ui.key(xd, host, 'x', ('Super_L',))
        ui.wait(lambda: get('/api/xr-control')['input_realm'] == 'xr')
        ui.check(report()['tabs'][0]['page'] == page, 'Super+X switches desktop/XR focus without triggering Miro x binding')
        viewer.terminate(); viewer.wait(timeout=10)
        ui.wait(lambda: all(w['id'] != window['id'] for w in get('/api/state')['scene']['windows']))
        ui.check(True, 'Closing FLTK removes its Wayland surface and menu registration')
        xd.close()
        (ui.out / 'checks.json').write_text(json.dumps(ui.checks, indent=2)+'\n')
finally:
    stop.set()
    if comp and comp.poll() is None:
        os.killpg(comp.pid, signal.SIGTERM)
    for child in reversed(ui.processes):
        if child.poll() is None:
            child.terminate()
            try: child.wait(timeout=8)
            except subprocess.TimeoutExpired: child.kill(); child.wait()
    for log in ui.logs: log.close()
    if 'temporary' in globals():
        import shutil
        shutil.rmtree(temporary, ignore_errors=True)
