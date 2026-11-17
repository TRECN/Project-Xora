
import xora2d
class AuditBindings:

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
        if not self.entity.is_valid():
            self._write_state("invalid")
            return
        name = self.entity.name
        uid = self.entity.uuid
        speed = float(getattr(self, "speed", -1.0))
        flag = bool(getattr(self, "enabled_flag", False))
        label = str(getattr(self, "label", ""))
        self.entity.transform.set_position(4.5, 6.5)
        self._write_state(f"{name}|{uid}|{speed}|{flag}|{label}")
