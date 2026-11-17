import xora2d


class PhysicsController:
    def on_create(self):
        self.jump_impulse = getattr(self, "jump_impulse", 7.0)

    def on_update(self, delta_time):
        del delta_time
        if not self.entity.is_valid():
            return
        if xora2d.Input.is_key_pressed(xora2d.Key.Space):
            self.entity.physics.apply_linear_impulse(0.0, self.jump_impulse)