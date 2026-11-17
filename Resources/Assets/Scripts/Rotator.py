import xora2d


class Rotator:
    def on_create(self):
        self.radians_per_second = getattr(self, "radians_per_second", 1.5)

    def on_update(self, delta_time):
        if not self.entity.is_valid():
            return
        rotation = self.entity.transform.rotation
        self.entity.transform.set_rotation(rotation + self.radians_per_second * delta_time)