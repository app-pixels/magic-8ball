> Part of [**app-pixels.com**](https://www.app-pixels.com) — browse + flash this app at [`/apps/magic-8ball`](https://www.app-pixels.com/apps/magic-8ball).

# magic-8ball

**Magic 8 Ball** · v1.0.0

Ask a yes-or-no question and shake.

**Hardware:** Waveshare ESP32-S3 1.8" AMOLED Touch

**Tags:** `#fun` `#offline`

Twenty answers, German or English. Shake the device or press a button.

## Controls
- Shake — reveal an answer
- **BOOT** — reveal without shaking

## `setup.txt` keys (optional)
- `BALL_LANGUAGE` — `de` (default) or `en`

## Editing `setup.txt`
The device reads `/setup/setup.txt` from the SD card on boot. [Download a working sample](https://sosbxffigpteqilpgxwn.supabase.co/storage/v1/object/public/app-assets/setup/setup.txt) — covers every app — and edit the keys you need.

Don't want to eject the card? Use the [**USB Stick**](/apps/usb-stick) app (mounts the SD card as a USB drive over USB-C) or the [**Filehub**](/apps/filehub) app (edit over WiFi).

## Build

1. Install [arduino-cli](https://arduino.github.io/arduino-cli/) or Arduino IDE 2.x.
2. Add the ESP32 board package (≥ 3.1.0):

   ```
   arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core install esp32:esp32 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Install the required Arduino libraries:

   - Adafruit XCA9554
   - GFX Library for Arduino (moononournation)
   - SensorLib (lewishe)
   - XPowersLib (lewishe)

4. Compile and upload:

   ```
   FQBN='esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600,LoopCore=1,EventsCore=1'
   arduino-cli compile -b "$FQBN" --build-path /tmp/magic-8ball_build .
   arduino-cli upload  -b "$FQBN" --input-dir /tmp/magic-8ball_build -p /dev/ttyACM0 .
   ```

   For browser flashing without a build environment, use the [pre-built binary](https://www.app-pixels.com/apps/magic-8ball).

## License

MIT — see [LICENSE](LICENSE). Do whatever you want with it.

---

Part of the [app-pixels.com](https://www.app-pixels.com) catalogue · live listing: https://www.app-pixels.com/apps/magic-8ball
