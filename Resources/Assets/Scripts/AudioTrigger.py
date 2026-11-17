import xora2d


class AudioTrigger:
    def on_create(self):
        self.asset_path = getattr(self, "asset_path", "Audio/sfx_beep.wav")

    def on_update(self, delta_time):
        del delta_time
        if xora2d.Input.is_key_pressed(xora2d.Key.Digit1):
            xora2d.Audio.play(self.asset_path)