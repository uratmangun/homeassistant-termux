# Home Assistant on Termux with Cloudflared

Run Home Assistant Core directly on Android using Termux, managed by termux-services, and exposed via Cloudflare Tunnel.

## Prerequisites

- Android device with [Termux](https://f-droid.org/packages/com.termux/) installed (from F-Droid)
- [Termux:Boot](https://f-droid.org/packages/com.termux.boot/) for auto-start on device boot
- Cloudflare account with a domain

## Installation

### 1. Install Required Packages

```bash
pkg update && pkg upgrade -y
pkg install -y python rust libffi openssl cmake libjpeg-turbo libpng zlib freetype c-ares python-numpy termux-services cloudflared
```

### 2. Create Python Virtual Environment

```bash
python3 -m venv ~/homeassistant-venv
~/homeassistant-venv/bin/pip install --upgrade pip wheel setuptools cython
```

### 3. Link System NumPy (Required for Termux)

```bash
ln -sf $PREFIX/lib/python3.12/site-packages/numpy ~/homeassistant-venv/lib/python3.12/site-packages/
ln -sf $PREFIX/lib/python3.12/site-packages/numpy*.dist-info ~/homeassistant-venv/lib/python3.12/site-packages/
```

### 4. Install Home Assistant

```bash
# Install HA without deps first
~/homeassistant-venv/bin/pip install homeassistant --no-deps

# Install core dependencies
~/homeassistant-venv/bin/pip install \
  'aiohttp==3.11.11' 'PyYAML==6.0.2' 'voluptuous==0.15.2' 'Jinja2==3.1.5' \
  'attrs==24.2.0' 'aiodns==3.2.0' 'yarl==1.18.3' 'propcache==0.2.1' \
  'atomicwrites-homeassistant==1.4.1' 'awesomeversion==24.6.0' 'bcrypt==4.2.0' \
  'certifi' 'ciso8601==2.3.2' 'cronsim==2.6' 'cryptography==44.0.0' \
  'fnv-hash-fast==1.0.2' 'httpx==0.27.2' 'ifaddr==0.2.0' 'lru-dict==1.3.0' \
  'orjson==3.10.12' 'packaging' 'PyJWT==2.10.1' 'pyOpenSSL==24.3.0' \
  'python-slugify==8.0.4' 'requests==2.32.3' 'SQLAlchemy==2.0.36' \
  'ulid-transform==1.0.2' 'urllib3<2' 'voluptuous-openapi==0.0.5' \
  'voluptuous-serialize==2.6.0' 'aiohttp-cors==0.7.0' 'aiohttp-fast-zlib==0.2.0' \
  'aiozoneinfo==0.2.1' 'astral==2.2' 'async-interrupt==1.2.0' \
  'securetar==2024.11.0' 'webrtc-models==0.3.0' 'hass-nabucasa==0.87.0' \
  'home-assistant-bluetooth==1.13.0' 'psutil-home-assistant==0.0.1' \
  'aiohasupervisor==0.2.2b5' 'home-assistant-frontend' 'zeroconf' \
  'ha-ffmpeg' 'async-upnp-client' 'PyTurboJPEG' 'pyotp' 'PyQRCode' \
  'hassil' 'aiodhcpwatcher' 'aiodiscover' 'cached-ipaddress' 'pyserial'
```

### 5. Create Mock UV Module

Home Assistant requires `uv` which doesn't build on Termux. Create a mock:

```bash
mkdir -p ~/homeassistant-venv/lib/python3.12/site-packages/uv

cat > ~/homeassistant-venv/lib/python3.12/site-packages/uv/__init__.py << 'EOF'
"""Mock uv module for Home Assistant on Termux."""
__version__ = '0.5.8'
def find_uv_bin():
    return None
EOF

cat > ~/homeassistant-venv/lib/python3.12/site-packages/uv/__main__.py << 'EOF'
"""Mock uv __main__ that wraps pip for Home Assistant on Termux."""
import sys
import subprocess

def main():
    args = sys.argv[1:]
    if args and args[0] == 'pip':
        args = args[1:]
    if args and args[0] == 'install':
        pip_args = ['pip', 'install']
        skip_next = False
        for arg in args[1:]:
            if skip_next:
                skip_next = False
                continue
            if arg in ('--reinstall-package', '--python', '-p', '--reinstall', '--index-strategy'):
                skip_next = True
                continue
            if arg.startswith(('--reinstall', '--python', '--index-strategy')):
                continue
            pip_args.append(arg)
        result = subprocess.run([sys.executable, '-m'] + pip_args)
        sys.exit(result.returncode)
    result = subprocess.run([sys.executable, '-m', 'pip'] + args)
    sys.exit(result.returncode)

if __name__ == '__main__':
    main()
EOF
```

### 6. Create Config Directory

```bash
mkdir -p ~/homeassistant-config
```

### 7. Configure Home Assistant for Cloudflare

```bash
cat > ~/homeassistant-config/configuration.yaml << 'EOF'
default_config:

http:
  use_x_forwarded_for: true
  trusted_proxies:
    - 127.0.0.1
    - ::1

frontend:
  themes: !include_dir_merge_named themes

automation: !include automations.yaml
script: !include scripts.yaml
scene: !include scenes.yaml
EOF

mkdir -p ~/homeassistant-config/themes
touch ~/homeassistant-config/automations.yaml
touch ~/homeassistant-config/scripts.yaml
touch ~/homeassistant-config/scenes.yaml
```

## Termux Services Setup

### 1. Create Home Assistant Service

```bash
mkdir -p $PREFIX/etc/sv/homeassistant/log

cat > $PREFIX/etc/sv/homeassistant/run << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
exec 2>&1
exec /data/data/com.termux/files/home/homeassistant-venv/bin/hass -c /data/data/com.termux/files/home/homeassistant-config
EOF
chmod +x $PREFIX/etc/sv/homeassistant/run

cat > $PREFIX/etc/sv/homeassistant/log/run << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
exec svlogd -tt ./main
EOF
chmod +x $PREFIX/etc/sv/homeassistant/log/run
mkdir -p $PREFIX/etc/sv/homeassistant/log/main

ln -sf $PREFIX/etc/sv/homeassistant $PREFIX/var/service/
```

### 2. Configure Cloudflared Tunnel

```bash
# Login to Cloudflare
cloudflared tunnel login

# Create tunnel
cloudflared tunnel create my-tunnel

# Route DNS (replace with your domain)
cloudflared tunnel route dns my-tunnel smarthome.yourdomain.com

# Create config
mkdir -p ~/.cloudflared
cat > ~/.cloudflared/config.yml << 'EOF'
tunnel: my-tunnel
credentials-file: /data/data/com.termux/files/home/.cloudflared/<TUNNEL_ID>.json

ingress:
  - hostname: smarthome.yourdomain.com
    service: http://localhost:8123
  - service: http_status:404
EOF
```

### 3. Create Cloudflared Service

```bash
mkdir -p $PREFIX/etc/sv/cloudflared/log

cat > $PREFIX/etc/sv/cloudflared/run << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
exec 2>&1
exec cloudflared tunnel run my-tunnel
EOF
chmod +x $PREFIX/etc/sv/cloudflared/run

cat > $PREFIX/etc/sv/cloudflared/log/run << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
exec svlogd -tt ./main
EOF
chmod +x $PREFIX/etc/sv/cloudflared/log/run
mkdir -p $PREFIX/etc/sv/cloudflared/log/main

ln -sf $PREFIX/etc/sv/cloudflared $PREFIX/var/service/
```

## Service Management

```bash
# Check status
SVDIR=$PREFIX/var/service sv status homeassistant
SVDIR=$PREFIX/var/service sv status cloudflared

# Start/Stop/Restart
SVDIR=$PREFIX/var/service sv up homeassistant
SVDIR=$PREFIX/var/service sv down homeassistant
SVDIR=$PREFIX/var/service sv restart homeassistant

# View logs
cat $PREFIX/etc/sv/homeassistant/log/main/current | tail -50
cat $PREFIX/etc/sv/cloudflared/log/main/current | tail -50
```

## Log Management (Remote via SSH)

Home Assistant logs can grow very large and fill up storage. Here's how to manage them remotely.

### Check Log Size

```bash
ssh -p 8022 u0_a319@<device-ip> "du -sh ~/homeassistant-config/*.log*"
```

### Clear All Logs

```bash
# Remove old rotated logs and truncate current log
ssh -p 8022 u0_a319@<device-ip> "rm -f ~/homeassistant-config/home-assistant.log.* && : > ~/homeassistant-config/home-assistant.log"
```

### Restart Home Assistant Service

```bash
ssh -p 8022 u0_a319@<device-ip> "SVDIR=\$PREFIX/var/service sv restart homeassistant"
```

### Restart Cloudflared Service

```bash
ssh -p 8022 u0_a319@<device-ip> "SVDIR=\$PREFIX/var/service sv restart cloudflared"
```

### Check Service Status

```bash
ssh -p 8022 u0_a319@<device-ip> "SVDIR=\$PREFIX/var/service sv status homeassistant cloudflared"
```

### Disable Logging (Recommended for Low Storage)

Add this to `~/homeassistant-config/configuration.yaml` to minimize logging:

```yaml
# Only log critical errors
logger:
  default: critical
  logs:
    homeassistant: critical
    homeassistant.core: critical
    homeassistant.components: critical

# Keep recorder data for only 1 day
recorder:
  purge_keep_days: 1
  commit_interval: 60
```

Then restart Home Assistant:

```bash
ssh -p 8022 u0_a319@<device-ip> "SVDIR=\$PREFIX/var/service sv restart homeassistant"
```

## Auto-Start on Boot

Install Termux:Boot from F-Droid, open it once, then:

```bash
mkdir -p ~/.termux/boot
cat > ~/.termux/boot/start-services.sh << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
termux-wake-lock
sshd
. $PREFIX/etc/profile
sv-enable homeassistant
sv-enable cloudflared
EOF
chmod +x ~/.termux/boot/start-services.sh
```

## Access

- Local: http://localhost:8123
- External: https://smarthome.yourdomain.com

## Battery Charger Daemon

A memory-efficient C program (~14KB) that automatically controls a smart power strip socket based on your Android battery level. It turns ON charging when battery drops below 30% and turns OFF when it exceeds 90%.

### Why C?

C is the most memory-efficient programming language, using minimal resources compared to Python or Node.js alternatives that would consume 50MB+.

### Prerequisites

```bash
pkg install -y clang libcurl termux-api
```

Make sure Termux:API app is also installed from F-Droid.

### Configuration

1. Copy the example environment file:

```bash
cp .env.example ~/.env
```

2. Edit `~/.env` with your Home Assistant credentials:

```bash
HOMEASSISTANT_HOST=localhost
HOMEASSISTANT_TOKEN=your_long_lived_access_token_here
ENTITY_ID=switch.smart_power_strip_socket_4
```

To generate a long-lived access token in Home Assistant:
- Go to your Profile → Security → Long-Lived Access Tokens → Create Token

### Building

```bash
clang -O2 -o ~/battery_charger battery_charger.c -lcurl
```

### Manual Testing

```bash
# Run once to verify it works
timeout 5 ~/battery_charger
```

Expected output:
```
Battery Charger Daemon Started
  Host: http://localhost:8123
  Entity: switch.smart_power_strip_socket_4
  Low threshold: 30%
  High threshold: 90%
  Check interval: 60 seconds

Battery: 45% | Switch: OFF -> No action needed
```

### Running as Termux Service

1. Create the service directory:

```bash
mkdir -p $PREFIX/var/service/battery_charger/log
```

2. Create the run script:

```bash
cat > $PREFIX/var/service/battery_charger/run << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
exec 2>&1
exec /data/data/com.termux/files/home/battery_charger
EOF
chmod +x $PREFIX/var/service/battery_charger/run
```

3. Create the log script:

```bash
cat > $PREFIX/var/service/battery_charger/log/run << 'EOF'
#!/data/data/com.termux/files/usr/bin/sh
mkdir -p $PREFIX/var/log/battery_charger
exec svlogd -tt $PREFIX/var/log/battery_charger
EOF
chmod +x $PREFIX/var/service/battery_charger/log/run
```

### Service Management

```bash
# Check status
sv status battery_charger

# Start/Stop/Restart
sv up battery_charger
sv down battery_charger
sv restart battery_charger
```

### Checking Logs

The service logs are stored in `$PREFIX/var/log/battery_charger/` and managed by `svlogd`.

```bash
# View recent logs (last 20 lines)
cat $PREFIX/var/log/battery_charger/current | tail -20

# View more logs (last 100 lines)
cat $PREFIX/var/log/battery_charger/current | tail -100

# Follow logs in real-time
tail -f $PREFIX/var/log/battery_charger/current

# Search logs for specific events
grep "Turn ON" $PREFIX/var/log/battery_charger/current
grep "Turn OFF" $PREFIX/var/log/battery_charger/current
```

Log entries include timestamps and show battery percentage, switch state, and actions taken:
```
2025-12-24_22:39:06.63325 Battery: 92% | Switch: OFF -> No action needed
2025-12-24_08:15:32.12345 Battery: 29% | Switch: OFF -> Turning ON charger
2025-12-24_12:45:18.98765 Battery: 91% | Switch: ON -> Turning OFF charger
```

**Via SSH:** When running commands remotely, escape `$PREFIX` to prevent local shell expansion:
```bash
ssh -p 8022 user@device "cat \$PREFIX/var/log/battery_charger/current | tail -100"
```

### How It Works

| Battery Level | Charger State | Action |
|---------------|---------------|--------|
| < 30% | OFF | Turn ON charger |
| > 90% | ON | Turn OFF charger |
| 30-90% | Any | No action |

The daemon checks battery status every 60 seconds using `termux-battery-status` and controls the smart socket via Home Assistant REST API.

### Home Assistant API Endpoints Used

- `GET /api/states/{entity_id}` - Check current switch state
- `POST /api/services/switch/turn_on` - Turn on the socket
- `POST /api/services/switch/turn_off` - Turn off the socket

## Preventing Android from Killing Termux

Android aggressively kills background apps. Use these ADB commands to make Termux persistent:

### 1. Connect via ADB (Wireless Debugging)

```bash
adb connect <device-ip>:<wireless-debugging-port>
```

### 2. Add Termux to Battery Optimization Whitelist (Doze Mode)

```bash
adb shell "dumpsys deviceidle whitelist +com.termux"
```

### 3. Allow Background Execution

```bash
adb shell "cmd appops set com.termux RUN_IN_BACKGROUND allow"
adb shell "cmd appops set com.termux RUN_ANY_IN_BACKGROUND allow"
adb shell "cmd appops set com.termux START_FOREGROUND allow"
adb shell "cmd appops set com.termux FOREGROUND_SERVICE allow"
adb shell "cmd appops set com.termux BOOT_COMPLETED allow"
```

### 4. Start Termux via ADB (if killed)

```bash
# Open Termux app
adb shell "am start -n com.termux/.app.TermuxActivity"

# Type and execute sshd command
adb shell "input text 'sshd' && input keyevent 66"
```

### 5. Verify Whitelist

```bash
adb shell "dumpsys deviceidle whitelist" | grep termux
```

### Additional Device Settings

1. **Battery Settings**: Set Termux to "Unrestricted" in battery optimization
2. **Termux Notification**: Keep the wake lock notification visible (shows "Termux is running")
3. **Termux:Boot**: Install from F-Droid for auto-start on device boot
4. **Wake Lock**: Run `termux-wake-lock` to prevent device sleep

### MIUI/Xiaomi Specific (Disable PowerKeeper)

MIUI has aggressive background app killing. Disable PowerKeeper's ability to kill Termux:

```bash
# Prevent PowerKeeper from modifying settings
adb shell "cmd appops set com.miui.powerkeeper WRITE_SETTINGS deny"

# Block PowerKeeper from tracking app usage
adb shell "cmd appops set com.miui.powerkeeper GET_USAGE_STATS deny"

# Disable PowerKeeper's background monitoring
adb shell "cmd appops set com.miui.powerkeeper RUN_IN_BACKGROUND deny"
```

### MIUI Settings UI (via ADB)

```bash
# Open AutoStart Management - Enable Termux here
adb shell "am start -n com.miui.securitycenter/com.miui.permcenter.autostart.AutoStartManagementActivity"

# Open Battery Saver settings
adb shell "am start -n com.miui.securitycenter/com.miui.powercenter.PowerSettings"

# Open Lock Apps settings directly (MIUI 12.5+)
adb shell "am start -n com.miui.securitycenter/com.miui.powercenter.SpeedBoostLockAppsActivity"
```

### Lock Apps in MIUI (Prevent "Clear All" from Killing Termux)

This is the **most important setting** to prevent Termux from being killed when you tap "Clear All" in recent apps.

**Method 1: Via ADB (opens the settings screen)**
```bash
adb shell "am start -n com.miui.securitycenter/com.miui.powercenter.SpeedBoostLockAppsActivity"
```
Then toggle ON **Termux** in the list.

**Method 2: Manual Navigation (MIUI 12.5+)**
1. Open the **Security** app
2. Tap the **Settings icon** (gear) in the top-right corner
3. Scroll down and tap **Boost speed**
4. Tap **Lock apps**
5. Find **Termux** and toggle it **ON**

**Method 3: From Recent Apps (Older MIUI versions)**
1. Open Termux
2. Open recent apps (swipe up and hold)
3. Long-press on the Termux card
4. Tap the **padlock icon** to lock it

Once locked, Termux will show a small lock icon in recent apps and will **NOT be killed** when you tap "Clear All".

## Notes

- First startup takes a while as HA downloads additional components
- Some integrations may not work due to Termux limitations (Bluetooth, USB)
- Even with all settings, swiping away Termux from recent apps may still kill it on some devices
- Use Termux:Boot + wake lock for best persistence