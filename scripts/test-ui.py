#!/usr/bin/env python3
"""Real FLTK/GL/DBusMenu tests in private X11 and optional nested DomeD sessions.
Run: uv run --with python-xlib --with pillow --with dbus-next scripts/test-ui.py
"""
import asyncio
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from PIL import Image
from Xlib import X, XK, display
from Xlib.ext import xtest
from dbus_next import Message, MessageType, Variant
from dbus_next.aio import MessageBus

signal.pthread_sigmask(signal.SIG_SETMASK, [])
if __name__ == '__main__' and os.environ.get('MIRO_TEST_BUS') != '1':
    os.execvpe('dbus-run-session', ['dbus-run-session', '--', sys.executable, *sys.argv],
              {**os.environ, 'MIRO_TEST_BUS': '1'})
root = Path(__file__).resolve().parents[1]
binary = Path(os.environ.get('MIRO_BINARY', root / 'build/miro-pdf'))
out = root / 'artifacts/ui'
out.mkdir(parents=True, exist_ok=True)
checks, processes, logs = [], [], []

def check(condition, description):
    if not condition:
        raise AssertionError(description)
    checks.append(description)
    print('PASS', description, flush=True)

def start(args, name, env, **kwargs):
    log = (out / (name + '.log')).open('w')
    logs.append(log)
    child = subprocess.Popen([str(a) for a in args], env=env, stdin=subprocess.DEVNULL,
                             stdout=log, stderr=subprocess.STDOUT, **kwargs)
    processes.append(child)
    return child

def wait(predicate, timeout=40):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (FileNotFoundError, json.JSONDecodeError, ConnectionError, OSError):
            pass
        time.sleep(.08)
    raise AssertionError('Timed out waiting for ' + repr(predicate))

async def call(service, path, member, signature='', body=None):
    bus = await MessageBus().connect()
    try:
        reply = await asyncio.wait_for(bus.call(Message(destination=service, path=path,
            interface='com.canonical.dbusmenu', member=member, signature=signature, body=body or [])), 5)
        assert reply.message_type != MessageType.ERROR, reply.body
        return reply.body
    finally:
        bus.disconnect()

def menu(report):
    return asyncio.run(call(report['menu_service'], report['menu_path'], 'GetLayout', 'iias', [0, -1, []]))[1]

def nodes(node):
    yield node
    for value in node[2]:
        yield from nodes(value.value)

def item(report, name):
    for node in nodes(menu(report)):
        label = node[1].get('label')
        if label and label.value.replace('_', '') == name:
            return node
    raise AssertionError('Missing menu item ' + name)

def activate(report, name):
    node = item(report, name)
    asyncio.run(call(report['menu_service'], report['menu_path'], 'Event', 'isvu',
                    [node[0], 'clicked', Variant('i', 0), 0]))

def key(xd, window, name, modifiers=()):
    window.set_input_focus(X.RevertToParent, X.CurrentTime)
    codes = [xd.keysym_to_keycode(XK.string_to_keysym(m)) for m in modifiers]
    code = xd.keysym_to_keycode(XK.string_to_keysym(name))
    for c in codes:
        xtest.fake_input(xd, X.KeyPress, c)
    xtest.fake_input(xd, X.KeyPress, code)
    xtest.fake_input(xd, X.KeyRelease, code)
    for c in reversed(codes):
        xtest.fake_input(xd, X.KeyRelease, c)
    xd.sync()
    time.sleep(.15)

def main():
    with tempfile.TemporaryDirectory(prefix='miro-native-test-') as directory:
        temporary = Path(directory)
        home = temporary / 'home'
        config = home / '.config'
        (config / 'dome').mkdir(parents=True)
        (config / 'miro-pdf').mkdir()
        runtime = temporary / 'runtime'
        runtime.mkdir(mode=0o700)
        appearance = config / 'dome/appearance.json'
        def theme(light, scale=1):
            appearance.write_text(json.dumps({'font': 'Unifont / Sans-Serif', 'font_size': 16,
                                             'font_scale': scale, 'light': light, 'accent': '#6babff'}))
        theme(False)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            rpc_port = sock.getsockname()[1]
        settings = 'Bind x Normal NextPage\nBind "g g" Normal ZoomHome\nSet Rpc True\nSet RpcPort '+str(rpc_port)+'\n'
        (config / 'miro-pdf/miro.conf').write_text(settings)
        rd, wr = os.pipe()
        start(['Xvfb', '-displayfd', str(wr), '-screen', '0', '1800x1200x24', '-nolisten', 'tcp'],
              'xvfb', os.environ.copy(), pass_fds=(wr,))
        os.close(wr)
        with os.fdopen(rd) as pipe:
            ds = ':' + pipe.readline().strip()
        env = {**os.environ, 'HOME': str(home), 'DISPLAY': ds, 'XDG_CONFIG_HOME': str(config),
               'XDG_CACHE_HOME': str(home / '.cache'), 'XDG_DATA_HOME': str(home / '.local/share'),
               'XDG_RUNTIME_DIR': str(runtime), 'XDG_CURRENT_DESKTOP': 'Dome',
               'LIBGL_ALWAYS_SOFTWARE': '1', 'DE_XR_APPEARANCE_TRACE': '1', 'GIO_USE_VFS': 'local'}
        env.pop('WAYLAND_DISPLAY', None)
        env.pop('WAYLAND_SOCKET', None)
        env.pop('FONTCONFIG_FILE', None)
        report_file = out / 'x11.json'
        report_file.unlink(missing_ok=True)
        sample = temporary / 'Miro český 文書.pdf'
        sample.write_bytes((root / 'assets/multiple-page-layouts.pdf').read_bytes())
        child = start([binary, sample, root / 'assets/text-copy-test.pdf', '--report', report_file], 'viewer', env)
        def report():
            assert child.poll() is None, (out / 'viewer.log').read_text()
            return json.loads(report_file.read_text())
        def state_when(predicate):
            value = report()
            return value if predicate(value) else None
        state = wait(lambda: state_when(lambda r: len(r['tabs']) == 2 and all(t['ready'] for t in r['tabs']) and r['uploaded_tiles']))
        check(state['toolkit'] == 'FLTK' and state['font_file'].lower().endswith('.bdf'), 'Native FLTK uses a raw BDF UI font')
        check(state['font_size'] == 16, 'Native 16 pixel font strike is selected')
        check(not state['config_errors'], 'Upstream-compatible binding configuration loads')
        check(state['menu_attached'], 'X11 window exports its native DBusMenu association')
        check(item(state, 'Open…')[1]['shortcut'].value == [['Control', 'o']], 'Universal menu publishes actual configured shortcuts')
        check(item(state, 'Dark Document')[1]['toggle-state'].value == 1, 'Universal menu publishes document toggle state')
        activate(state, 'Dark Document')
        wait(lambda: not report()['tabs'][1]['dark'])
        check(True, 'DBusMenu activates the real document rendering action')
        activate(state, 'Two Pages')
        wait(lambda: report()['tabs'][1]['layout'] == 1)
        check(item(state, 'Two Pages')[1]['toggle-state'].value == 1 and item(state, 'Single Page')[1]['toggle-state'].value == 0,
              'Layout radio state follows native menu actions')
        xd = display.Display(ds)
        window = wait(lambda: next((w for w in xd.screen().root.query_tree().children if 'miro-pdf' in (w.get_wm_class() or ())), None))
        key(xd, window, 'Tab', ('Control_L', 'Shift_L'))
        wait(lambda: report()['active'] == 0)
        check(True, 'Ctrl+Shift+Tab switches native tabs')
        key(xd, window, 'x')
        wait(lambda: report()['tabs'][0]['page'] == 1)
        check(True, 'User-defined application key binding changes page')
        key(xd, window, 'x', ('Super_L',))
        time.sleep(1.2)
        check(report()['tabs'][0]['page'] == 1, 'Super+X is not consumed as the user x binding')
        key(xd, window, 'g'); key(xd, window, 'g')
        wait(lambda: abs(report()['tabs'][0]['scale'] - 1) < .001)
        check(True, 'Upstream multi-key chords are interpreted')
        key(xd, window, 'Tab', ('Control_L',))
        wait(lambda: report()['active'] == 1)
        key(xd, window, 'a', ('Control_L',))
        wait(lambda: bool(report()['tabs'][1]['selection']))
        check(True, 'Selectable PDF text reaches the native clipboard path')
        key(xd, window, 'f', ('Control_L',))
        for char in 'systems':
            key(xd, window, char)
        key(xd, window, 'Return')
        wait(lambda: report()['tabs'][1]['matches'] > 0)
        check(True, 'Native search input performs PDF text search')
        key(xd, window, 'Escape')
        before = report()['font_revision']
        theme(True, 2)
        state = wait(lambda: state_when(lambda r: r['font_size'] == 32 and r['light']))
        check(state['font_revision'] > before and state['font_file'].lower().endswith('.bdf'), 'Live light theme and 2x BDF enlargement apply without conversion')
        check(state['tabs'][0]['dark'] is False, 'Unmodified PDF background follows the live appearance palette')
        check((config / 'miro-pdf/miro.conf').read_text() == settings, 'Session appearance leaves user Miro configuration intact')
        def light_frame():
            geometry = window.get_geometry()
            raw = window.get_image(0, 0, geometry.width, geometry.height, X.ZPixmap, 0xffffffff)
            image = Image.frombytes('RGB', (geometry.width, geometry.height), raw.data, 'raw', 'BGRX')
            return image if image.getpixel((2, image.height-2))[0] > 180 else None
        screenshot = wait(light_frame)
        screenshot.save(out / 'light-2x.png')
        text = screenshot.crop((20, 6, min(500, screenshot.width-100), 46))
        pixels = list(text.get_flattened_data())
        ink = [(i % text.width, i // text.width) for i,(r,g,b) in enumerate(pixels) if r < 80 and g < 80 and b < 80]
        check(len(ink) > 200 and max(y for x,y in ink)-min(y for x,y in ink) >= 16,
              'Native GL controls contain visibly enlarged 2x BDF glyph pixels')
        libraries = subprocess.check_output(['ldd', str(binary)], text=True)
        check('pango' not in libraries.lower() and 'not found' not in libraries, 'Executable and its loaded toolkit dependencies do not require Pango')
        def rpc(kind, data=None):
            value = {'message': {'type': kind}}
            if data is not None:
                value['message']['data'] = data
            request = urllib.request.Request(f'http://127.0.0.1:{rpc_port}/', data=json.dumps(value).encode(), headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=5) as response:
                assert response.status == 200
        rpc('CloseFile', {'path': str(sample)})
        wait(lambda: len(report()['tabs']) == 1)
        rpc('OpenFile', {'path': str(sample)})
        wait(lambda: len(report()['tabs']) == 2 and report()['tabs'][1]['ready'])
        check(True, 'Optional upstream RPC opens and closes PDF tabs')
        # Reload through a real file modification; the annotation fixture has a different page count.
        old_epoch = report()['tabs'][1]['epoch']
        sample.write_bytes((root / 'assets/links_annotated.pdf').read_bytes())
        wait(lambda: report()['tabs'][1]['epoch'] > old_epoch and report()['tabs'][1]['ready'])
        check(True, 'File monitoring reloads a changed PDF on the worker')
        activate(report(), 'Close Tab')
        wait(lambda: len(report()['tabs']) == 1)
        activate(report(), 'Close Tab')
        wait(lambda: not report()['tabs'])
        check(not item(report(), 'Save Copy…')[1].get('enabled', Variant('b', True)).value,
              'Document menu actions are disabled without a PDF')
        activate(report(), 'Quit')
        child.wait(timeout=10)
        check(child.returncode == 0, 'Viewer closes and joins the document worker cleanly')
        check((config / 'miro-pdf/fltk-state.json').is_file(), 'Native session state persists separately from miro.conf')
        xd.close()
    (out / 'checks.json').write_text(json.dumps(checks, indent=2)+'\n')

if __name__ == '__main__':
    try:
        main()
    finally:
        for child in reversed(processes):
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait()
        for log in logs:
            log.close()
