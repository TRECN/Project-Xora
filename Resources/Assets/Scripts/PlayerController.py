import xora2d


class PlayerController:
  def on_create(self):
    # speed: horizontal movement in meters per second (physics units).
    self.speed = float(getattr(self, "speed", 4.5))
    self.jump_impulse = float(getattr(self, "jump_impulse", 7.0))
    self.display_name = getattr(self, "display_name", "Player")
    xora2d.Log.info(f"{self.display_name} created")

  def on_update(self, delta_time):
    if not self.entity.is_valid():
      return

    vel = self.entity.physics.get_linear_velocity()
    vx, vy = float(vel[0]), float(vel[1])

    target_x = 0.0
    if xora2d.Input.is_key_down(xora2d.Key.A) or xora2d.Input.is_key_down(xora2d.Key.Left):
      target_x -= self.speed
    if xora2d.Input.is_key_down(xora2d.Key.D) or xora2d.Input.is_key_down(xora2d.Key.Right):
      target_x += self.speed
    vx = target_x

    if xora2d.Input.is_key_down(xora2d.Key.W) or xora2d.Input.is_key_down(xora2d.Key.Up):
      vy = self.speed
    elif xora2d.Input.is_key_down(xora2d.Key.S) or xora2d.Input.is_key_down(xora2d.Key.Down):
      vy = -self.speed

    self.entity.physics.set_linear_velocity(vx, vy)

    if xora2d.Input.is_key_pressed(xora2d.Key.Space):
      self.entity.physics.apply_linear_impulse(0.0, self.jump_impulse)

  def on_destroy(self):
    xora2d.Log.info(f"{getattr(self, 'display_name', 'Player')} destroyed")