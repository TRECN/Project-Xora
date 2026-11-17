
class AuditTwin:

    def _state_path(self):
        return getattr(self, "state_path", "")
    def _read_state(self):
        path = self._state_path()
        if not path:
            return ""
        try:
            with open(path, "r") as handle:
                return handle.read()
        except OSError:
            return ""
    def _write_state(self, value):
        path = self._state_path()
        if not path:
            return
        with open(path, "w") as handle:
            handle.write(value)
    def _append_state(self, token):
        path = self._state_path()
        if not path:
            return
        with open(path, "a") as handle:
            handle.write(token)

    def on_create(self):
        marker = getattr(self, "marker", "?")
        self._write_state(marker)
    def on_update(self, dt):
        marker = self._read_state()
        if marker.endswith("u"):
            return
        self._write_state(marker + "u")
