"""A window of the test's own that accepts files dropped on it (OLE IDropTarget, as Explorer and chat programs are), for
checking drags out of the programs without depending on where Explorer opens on the desktop."""
import threading

import pythoncom
import win32con
import win32gui
from win32com.server.util import wrap

CLASS = "AnyCaptureE2EDropTarget"
DROPEFFECT_COPY = 1


class _Target:
    _public_methods_ = ["DragEnter", "DragOver", "DragLeave", "Drop"]
    _com_interfaces_ = [pythoncom.IID_IDropTarget]

    def __init__(self, owner):
        self.owner = owner

    def DragEnter(self, data, key_state, point, effect):
        return DROPEFFECT_COPY

    def DragOver(self, key_state, point, effect):
        return DROPEFFECT_COPY

    def DragLeave(self):
        pass

    def Drop(self, data, key_state, point, effect):
        fe = (win32con.CF_HDROP, None, pythoncom.DVASPECT_CONTENT, -1, pythoncom.TYMED_HGLOBAL)
        try:
            self.owner.files = _hdrop_files(data.GetData(fe))
        except Exception as e:     # noqa: BLE001  (reported through .error)
            self.owner.error = repr(e)
        return DROPEFFECT_COPY


def _hdrop_files(medium):
    import win32api
    h = medium.data_handle
    n = win32api.DragQueryFile(h, -1)
    return [win32api.DragQueryFile(h, i) for i in range(n)]


class DropTarget:
    """A topmost window at (x, y, w, h) on its own OLE thread; .files holds what was dropped on it."""

    def __init__(self, x, y, w, h):
        self.files, self.error, self.hwnd = None, None, 0
        self._ready = threading.Event()
        self._thread = threading.Thread(target=self._run, args=(x, y, w, h), daemon=True)
        self._thread.start()
        assert self._ready.wait(10), "the drop target window did not open"

    def _run(self, x, y, w, h):
        pythoncom.OleInitialize()
        wc = win32gui.WNDCLASS()
        wc.lpszClassName = CLASS
        wc.lpfnWndProc = {win32con.WM_DESTROY: self._destroyed}
        wc.hbrBackground = win32con.COLOR_WINDOW + 1
        try:
            win32gui.RegisterClass(wc)
        except win32gui.error:
            pass                    # registered by an earlier test
        self.hwnd = win32gui.CreateWindowEx(win32con.WS_EX_TOPMOST, CLASS, "AnyCapture e2e drop target",
                                            win32con.WS_POPUP | win32con.WS_VISIBLE | win32con.WS_BORDER, x, y, w, h, 0, 0, 0, None)
        pythoncom.RegisterDragDrop(self.hwnd, wrap(_Target(self), pythoncom.IID_IDropTarget, useDispatcher=False))
        self._ready.set()
        win32gui.PumpMessages()
        pythoncom.CoUninitialize()

    def _destroyed(self, hwnd, msg, wp, lp):
        try:
            pythoncom.RevokeDragDrop(hwnd)
        except pythoncom.com_error:
            pass
        win32gui.PostQuitMessage(0)
        return 0

    def close(self):
        if self.hwnd and win32gui.IsWindow(self.hwnd):
            win32gui.PostMessage(self.hwnd, win32con.WM_CLOSE, 0, 0)
        self._thread.join(5)
