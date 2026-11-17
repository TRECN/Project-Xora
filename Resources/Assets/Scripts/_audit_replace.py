
class AuditReplace:

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

    def on_update(self, dt):
        value = self._read_state()
        count = 0 if not value else int(value)
        self._write_state(str(count + 1))
