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

## Notes

- First startup takes a while as HA downloads additional components
- Some integrations may not work due to Termux limitations (Bluetooth, USB)
- Keep Termux running in background (disable battery optimization)
