// The video as a component: its setup, its line in the log, and the loop that tells Screen camera of a viewer and
// starts and stops the microphone. The frame path is video_encoder.cpp, the server rtsp_server.cpp, the sound
// opus_sound.cpp (README.md, "Video").
#include "screen_video.h"
#include "screen_video_shared.h"

#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::screen_video {

static constexpr uint32_t REPORT_MS = 60000;
static constexpr UBaseType_t SERVER_PRIORITY = 2;  // below the wake word's 3 (setup)

void ScreenVideo::setup() {
  const size_t internal_at_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  ready_ = xQueueCreate(SLOTS, sizeof(int));
  // The slots and the send batch are taken when the first viewer plays (take_room_, 0.7.0).
  if (ready_ == nullptr) {
    ESP_LOGE(TAG, "No memory for the video");
    mark_failed();
    return;
  }
  // The camera's raw frames in the encoder's own YUV420 (esp_video_camera device: csi), so no conversion at all,
  // through Screen camera's frame hub, which hands the same frames to its light level and motion. The video adds itself
  // first (its setup runs before Screen camera's), so the camera hands out this layout.
  if (camera_ == nullptr || screen_camera_ == nullptr ||
      !screen_camera_->frames().add(this, esp_video_camera::RawVideoPixelFormat::YUV420_OUYY_EVYY)) {
    ESP_LOGE(TAG, "The camera hands its frames to another consumer already");
    mark_failed();
    return;
  }
  // Nothing of the camera starts here (0.7.0): the loop turns it on when the first viewer plays, and the encoder is
  // made on the camera's task at that first frame and kept from then on. So a screen whose video nobody ever opens
  // gives the encoder none of its internal RAM, and its camera makes no frame. Before, the camera ran from boot so
  // the encoder could be made early, when a P4 had less internal RAM to spare than it has now. Never in setup():
  // made before the camera driver had run, the first encoded frame hung the whole chip.
#ifdef USE_SCREEN_VIDEO_SOUND
  setup_sound_();
#endif
  // Opus encodes on this task, and it takes a deep stack (about 27 KB): in PSRAM, as every big buffer of the video,
  // since internal RAM is what the screen runs short of. It runs below the wake word (micro_wake_word's inference at 3),
  // with the live tiles' streams (2): a viewer of the video on a D1001 cost the wake word 60 resets an hour at 4.
  if (xTaskCreatePinnedToCoreWithCaps(server_task_, "screen_video", 32768, this, SERVER_PRIORITY, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "No memory for the video's server");
    mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Set up; internal RAM: %u bytes free before, %u after", (unsigned) internal_at_start,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void ScreenVideo::dump_config() {
  ESP_LOGCONFIG(TAG, "Screen video:\n  RTSP port: %u\n  H.264: the camera's size (%ux%u so far), %u fps, %u kbit/s, "
                "a key frame every %u frames",
                port_, width_, height_, FRAMERATE, (unsigned) (BITRATE / 1000), GOP);
#ifdef USE_SCREEN_VIDEO_SOUND
  ESP_LOGCONFIG(TAG, "  Sound: Opus, %u Hz mono, 32 kbit/s%s", (unsigned) sound_rate_, opus_ ? "" : " (no encoder yet: it is opened for the first viewer)");
#endif
}

void ScreenVideo::loop() {
  // Screen camera's dot while anyone watches (the core sees Home Assistant's own pictures by itself, not this).
  const bool watched = playing_.load();
  // Frames for the encoder only while someone watches: else the frame hub halves no Full field of view's frame for
  // it (frame_hub.h).
  const bool picture = watched;
  if (picture != picture_said_ && screen_camera_ != nullptr) {
    picture_said_ = picture;
    screen_camera_->frames().wants(this, picture);
  }
  // And the camera itself runs for the video only then (0.7.0). With nobody watching and light level and motion off,
  // no consumer is on and the frame hub stops the camera: the sensor streams nothing, the ISP and the capture task
  // rest. Before, the video held the camera on from boot, at 5 frames a second for nobody.
  if (picture != camera_on_ && screen_camera_ != nullptr) {
    camera_on_ = picture;
    screen_camera_->frames().set_on(this, picture);
  }
  if (watched != watched_) {
    watched_ = watched;
    if (screen_camera_ != nullptr) {
      // The sensor at full speed while anyone watches, at once; slow again a while after the last one went.
      screen_camera_->frames().ask(this, watched ? screen_camera::frame_rate::FULL : screen_camera::frame_rate::NONE);
      screen_camera_->set_video_watched(watched);
    }
  }
#ifdef USE_SCREEN_VIDEO_SOUND
  // The microphone starts and stops on the loop, as everything else that listens does. ESPHome counts its listeners,
  // so the voice assistant keeps it while it listens too. And the screen hears of it (plugin API 0.23): while a viewer
  // has the sound, the screen keeps its live tiles at a quarter, so they do not starve this video's uplink.
  if (mic_ != nullptr && wants_sound_.load() != listening_) {
    listening_ = !listening_;
    if (listening_) {
      mic_->start();
    } else {
      mic_->stop();
    }
    if (screen_camera_ != nullptr) screen_camera_->set_sound_heard(listening_);
  }
#endif
  const uint32_t now = millis();
  if (now - reported_ms_ < REPORT_MS) return;
  const uint32_t span = now - reported_ms_;
  reported_ms_ = now;
  const uint32_t frames = frames_.load(), bytes = bytes_.load(), n = frames - reported_frames_;
  if (n > 0) {
    const uint32_t crop_us = crop_us_.load(), encode_us = encode_us_.load();
    ESP_LOGI(TAG, "%.1f fps, %u kbit/s, cutting %.1f ms and encoding %.1f ms a frame; since boot %u dropped, %u failed, "
             "%u skipped for internal RAM, %u left out for a busy screen, %u left behind for a full connection, %u bytes of "
             "internal RAM free, %d held by the encoder",
             n * 1000.0f / span, (unsigned) ((bytes - reported_bytes_) * 8ULL / span), (crop_us - reported_crop_us_) / 1000.0f / n,
             (encode_us - reported_encode_us_) / 1000.0f / n,
             (unsigned) dropped_.load(), (unsigned) failed_.load(), (unsigned) held_.load(), (unsigned) left_out_.load(),
             (unsigned) behind_.load(),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL), encoder_internal_.load());
    reported_crop_us_ = crop_us;
    reported_encode_us_ = encode_us;
  }
  reported_frames_ = frames;
  reported_bytes_ = bytes;
#ifdef USE_SCREEN_VIDEO_SOUND
  const uint32_t packets = sound_packets_.load(), opus_us = opus_us_.load();
  if (packets != reported_sound_)
    ESP_LOGI(TAG, "Sound: %u packets of 20 ms, %.1f ms encoding a packet, %u bytes lost since boot, %u packets left behind "
             "for a full connection",
             (unsigned) (packets - reported_sound_), (opus_us - reported_opus_us_) / 1000.0f / (packets - reported_sound_),
             (unsigned) sound_lost_.load(), (unsigned) sound_behind_.load());
  reported_sound_ = packets;
  reported_opus_us_ = opus_us;
#endif
}

}  // namespace esphome::screen_video
