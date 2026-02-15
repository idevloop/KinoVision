# SPDX-FileCopyrightText: 2025 KinoVision Project
# KinoVision Memento - MQTT Version
# Receives: gesture feed (SHAKE command)
# Publishes: device feed (LIGHT/NONE)

import os
import time
import gc
import binascii
import wifi
import socketpool
import ssl
import adafruit_requests
import adafruit_pycamera
import adafruit_minimqtt.adafruit_minimqtt as MQTT
import displayio
import terminalio
from adafruit_display_text import label

print("\n=== KINOVISION MEMENTO (MQTT) ===\n")

# -------------------------------------------------
# CONFIG
# -------------------------------------------------
WIFI_SSID = os.getenv("CIRCUITPY_WIFI_SSID")
WIFI_PASSWORD = os.getenv("CIRCUITPY_WIFI_PASSWORD")
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")

# Check if environment variables are set
if not WIFI_SSID or not WIFI_PASSWORD:
    print("ERROR: WiFi credentials not found in settings.toml!")
    print("Please add to settings.toml:")
    print('CIRCUITPY_WIFI_SSID = "YourWiFiName"')
    print('CIRCUITPY_WIFI_PASSWORD = "YourPassword"')
    while True:
        time.sleep(1)

if not OPENAI_API_KEY:
    print("ERROR: OpenAI API key not found in settings.toml!")
    print("Please add to settings.toml:")
    print('OPENAI_API_KEY = "sk-proj-..."')
    while True:
        time.sleep(1)

# Adafruit IO MQTT - MUST MATCH ESP32 HUB
AIO_USERNAME = "********"
AIO_KEY = "********"

# Feed names - MUST MATCH ESP32 HUB
GESTURE_FEED = AIO_USERNAME + "/feeds/gesture"  # Subscribe (receive SHAKE)
DEVICE_FEED = AIO_USERNAME + "/feeds/device"    # Publish (send LIGHT/NONE)

MODEL = "gpt-4o-mini"

PROMPT = """Look at this image and identify if there is any LIGHT SOURCE visible.
This includes: LED bulbs, LED strips, ceiling lights, table lamps, floor lamps, tube lights, or any illuminated light fixture.
Reply with ONLY ONE WORD:
- "light" if you see any light source
- "none" if no light source is visible
Reply with just one word, nothing else."""

current_device = None
mqtt_client = None

# -------------------------------------------------
# CAMERA INIT
# -------------------------------------------------
print("Initializing camera...")
pycam = adafruit_pycamera.PyCamera()
pycam.mode = 0
pycam.resolution = 2
pycam.effect = 0
pycam.led_level = 1
print("✓ Camera ready")

time.sleep(1)

# -------------------------------------------------
# WIFI
# -------------------------------------------------
print("Connecting WiFi...")
print(f"  SSID: {WIFI_SSID}")
wifi.radio.connect(WIFI_SSID, WIFI_PASSWORD)
print("✓ WiFi connected:", wifi.radio.ipv4_address)

pool = socketpool.SocketPool(wifi.radio)
requests = adafruit_requests.Session(pool, ssl.create_default_context())

# -------------------------------------------------
# DISPLAY HELPERS
# -------------------------------------------------
def show_status(message, color=0xFFFFFF, duration=2.0):
    """Show status message on screen"""
    try:
        # Create a solid color background
        display = pycam.display
        bitmap = displayio.Bitmap(display.width, display.height, 1)
        palette = displayio.Palette(1)
        palette[0] = 0x000000  # Black background

        tile_grid = displayio.TileGrid(bitmap, pixel_shader=palette)
        group = displayio.Group()
        group.append(tile_grid)

        # Add text
        text_area = label.Label(
            terminalio.FONT,
            text=message,
            color=color,
            scale=2
        )

        # Center the text
        text_area.x = (display.width - text_area.bounding_box[2]) // 2
        text_area.y = display.height // 2

        group.append(text_area)

        display.root_group = group
        time.sleep(duration)

        # Return to camera preview
        pycam.live_preview_mode()

    except Exception as e:
        print(f"Display error: {e}")

def show_multi_line_status(lines, color=0xFFFFFF, duration=2.0):
    """Show multi-line status message"""
    try:
        display = pycam.display
        bitmap = displayio.Bitmap(display.width, display.height, 1)
        palette = displayio.Palette(1)
        palette[0] = 0x000000  # Black background

        tile_grid = displayio.TileGrid(bitmap, pixel_shader=palette)
        group = displayio.Group()
        group.append(tile_grid)

        # Add multiple lines
        line_height = 30
        total_height = len(lines) * line_height
        start_y = (display.height - total_height) // 2 + 15

        for i, line in enumerate(lines):
            text_area = label.Label(
                terminalio.FONT,
                text=line,
                color=color,
                scale=2
            )
            text_area.x = (display.width - text_area.bounding_box[2]) // 2
            text_area.y = start_y + (i * line_height)
            group.append(text_area)

        display.root_group = group
        time.sleep(duration)

        # Return to camera preview
        pycam.live_preview_mode()

    except Exception as e:
        print(f"Display error: {e}")

# -------------------------------------------------
# HELPERS
# -------------------------------------------------
def encode_image(path):
    """Encode image to base64"""
    with open(path, "rb") as f:
        data = f.read()
        return binascii.b2a_base64(data).decode().strip()

def send_to_openai(image_path):
    """Send image to OpenAI for analysis"""
    print("Sending to OpenAI...")
    show_status("Analyzing...", 0x00FFFF, 1.0)

    base64_image = encode_image(image_path)

    headers = {
        "Authorization": f"Bearer {OPENAI_API_KEY}",
        "Content-Type": "application/json",
    }

    payload = {
        "model": MODEL,
        "messages": [{
            "role": "user",
            "content": [
                {"type": "text", "text": PROMPT},
                {
                    "type": "image_url",
                    "image_url": {
                        "url": f"data:image/jpeg;base64,{base64_image}",
                        "detail": "low"
                    }
                }
            ]
        }],
        "max_tokens": 20,
        "temperature": 0.2
    }

    try:
        r = requests.post(
            "https://api.openai.com/v1/chat/completions",
            headers=headers,
            json=payload,
            timeout=30
        )

        data = r.json()
        r.close()

        # Check for errors in response
        if "error" in data:
            print(f"OpenAI API Error: {data['error']['message']}")
            return None

        # Check if choices exists
        if "choices" not in data:
            print(f"No 'choices' in response")
            print(f"  Response keys: {data.keys()}")
            return None

        result = data["choices"][0]["message"]["content"].strip().lower()
        print(f"  ← OpenAI response: {result}")

        if "light" in result:
            return "light"
        else:
            return "none"

    except Exception as e:
        print(f"OpenAI error: {e}")
        return None

def capture_and_identify():
    """Capture image and identify device"""
    global current_device

    print("\n" + "=" * 40)
    print("  CAPTURE & IDENTIFY")
    print("=" * 40)

    try:
        # Show capturing status
        show_status("CAPTURING", 0xFFFF00, 0.5)

        # Enter camera mode
        pycam.live_preview_mode()
        time.sleep(0.2)

        # Capture image (let it auto-name)
        print("Capturing image...")
        pycam.capture_jpeg()
        time.sleep(0.5)

        # Find the most recent image
        files = [f for f in os.listdir("/sd") if f.endswith(".jpg")]
        if not files:
            print("No image file found!")
            show_multi_line_status(["ERROR:", "No Image"], 0xFF0000, 2.0)
            return None

        files.sort()
        image_path = "/sd/" + files[-1]

        print(f"Image saved: {image_path}")

        # Beep feedback
        try:
            pycam.tone(1000, 0.05)
        except:
            pass

        # Send to OpenAI (includes "Analyzing..." display)
        result = send_to_openai(image_path)

        if result and result.lower() != "none":
            current_device = result.strip()
            print(f"DETECTED: {current_device.upper()}")

            # Show success
            show_multi_line_status(
                ["DETECTED!", current_device.upper()],
                0x00FF00,  # Green
                3.0
            )

            # Success beep
            try:
                pycam.tone(1200, 0.1)
                time.sleep(0.05)
                pycam.tone(1600, 0.1)
            except:
                pass

            return current_device

        else:
            print("NO DEVICE DETECTED")
            current_device = None

            # Show failure
            show_multi_line_status(
                ["NO DEVICE", "DETECTED"],
                0xFF0000,  # Red
                2.0
            )

            # Failure beep
            try:
                pycam.tone(800, 0.2)
            except:
                pass

            return None

    except Exception as e:
        print(f"Capture error: {e}")
        show_status("ERROR!", 0xFF0000, 2.0)
        import traceback
        traceback.print_exception(e)
        current_device = None
        return None

    finally:
        print("=" * 40 + "\n")
        gc.collect()

def publish_result(device):
    """Publish device identification result to MQTT"""
    if device:
        result = device.upper()
    else:
        result = "NONE"

    print(f"  → Publishing to {DEVICE_FEED}: {result}")

    # Show publishing status
    show_status("Sending...", 0x00FFFF, 0.5)

    try:
        if mqtt_client.is_connected():
            mqtt_client.publish(DEVICE_FEED, result)
            print(f"Published: {result}")
            show_status("SENT!", 0x00FF00, 1.0)
            return True
        else:
            print("MQTT not connected!")
            show_status("MQTT ERROR", 0xFF0000, 2.0)
            return False
    except Exception as e:
        print(f"Publish error: {e}")
        show_status("SEND FAIL", 0xFF0000, 2.0)
        return False

# -------------------------------------------------
# MQTT SETUP
# -------------------------------------------------
print("Setting up MQTT...")

# MQTT callbacks
def connected(client, userdata, flags, rc):
    print("Connected to MQTT broker")
    print(f"Subscribing to: {GESTURE_FEED}")
    client.subscribe(GESTURE_FEED)
    print("Subscribed to gesture feed")

def disconnected(client, userdata, rc):
    print("Disconnected from MQTT")

def message_received(client, topic, message):
    """Handle incoming MQTT messages"""
    print("\n" + "="*40)
    print("MQTT MESSAGE RECEIVED")
    print(f"  Topic: {topic}")
    print(f"  Message: {message}")

    msg = message.strip().upper()

    if msg == "SHAKE":
        print("SHAKE command recognized")
        print("="*40)

        # Show MQTT command received
        show_status("SHAKE!", 0xFFFF00, 1.0)

        # Capture and identify
        device = capture_and_identify()

        # Publish result back to ESP32
        publish_result(device)

    else:
        print(f"Unknown command: {msg}")
        print(f"Expected: SHAKE")
        print("="*40 + "\n")

# Create MQTT client
print("  Creating MQTT client...")
mqtt_client = MQTT.MQTT(
    broker="io.adafruit.com",
    port=1883,
    username=AIO_USERNAME,
    password=AIO_KEY,
    socket_pool=pool,
    socket_timeout=1,
    keep_alive=60,
)

mqtt_client.on_connect = connected
mqtt_client.on_disconnect = disconnected
mqtt_client.on_message = message_received

# Connect to MQTT
print("  Connecting to MQTT broker...")
try:
    mqtt_client.connect()
    print("MQTT connected successfully")
except Exception as e:
    print(f"MQTT connection error: {e}")
    print("  Will retry in main loop...")

# -------------------------------------------------
# MAIN LOOP
# -------------------------------------------------
print("\n" + "="*40)
print("          SYSTEM READY")
print("="*40)
print("Listening for SHAKE commands...")
print(f"Subscribe: {GESTURE_FEED}")
print(f"Publish: {DEVICE_FEED}")
print("="*40 + "\n")

# Show ready status
show_status("READY", 0x00FF00, 2.0)

last_camera_update = time.monotonic()
last_mqtt_loop = time.monotonic()
mqtt_reconnect_time = 0
loop_count = 0

while True:
    try:
        now = time.monotonic()

        # ----- MQTT LOOP (HIGHEST PRIORITY) -----
        if now - last_mqtt_loop > 0.5:  # Check every 500ms
            try:
                # Check connection
                if not mqtt_client.is_connected():
                    if now - mqtt_reconnect_time > 5.0:
                        print("MQTT disconnected, reconnecting...")
                        try:
                            mqtt_client.reconnect()
                            mqtt_reconnect_time = now
                            print("Reconnected")
                        except Exception as e:
                            print(f"Reconnect failed: {e}")
                            mqtt_reconnect_time = now
                else:
                    # Process incoming messages
                    mqtt_client.loop(timeout=2.0)

                last_mqtt_loop = now

                # Status indicator every 60 loops (~30 seconds)
                loop_count += 1
                if loop_count % 60 == 0:
                    status = "Connected" if mqtt_client.is_connected() else "Disconnected"
                    print(f"[Status] MQTT: {status}")

            except Exception as e:
                print(f"MQTT loop error: {e}")
                mqtt_reconnect_time = now

        # ----- CAMERA PREVIEW -----
        if now - last_camera_update > 0.1:
            try:
                pycam.blit(pycam.continuous_capture())
                last_camera_update = now
            except:
                pass

        # ----- SHUTTER BUTTON (MANUAL CAPTURE) -----
        pycam.keys_debounce()
        if pycam.shutter.short_count:
            print("\nSHUTTER BUTTON PRESSED")
            device = capture_and_identify()
            publish_result(device)

        time.sleep(0.01)

    except KeyboardInterrupt:
        print("\n\n" + "="*40)
        print("  SHUTTING DOWN")
        print("="*40)
        try:
            mqtt_client.disconnect()
            print("MQTT disconnected")
        except:
            pass
        print("Program stopped\n")
        break

    except Exception as e:
        print(f"Error in main loop: {e}")
        time.sleep(0.1)
