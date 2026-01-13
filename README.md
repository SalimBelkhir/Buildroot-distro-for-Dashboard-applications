# Buildroot Distribution for IoT Dashboard Applications

A custom Buildroot-based Linux distribution for Raspberry Pi Zero W, designed for IoT dashboard applications with LVGL GUI and ESP32 sensor integration.

## Project Overview

This project demonstrates an end-to-end IoT system where:
**Raspberry Pi Zero W** acts as a TCP server running a custom LVGL-based dashboard
**ESP32 with DHT22 sensor** collects temperature and humidity data and sends it to the Pi
Real-time sensor data is displayed on an HDMI-connected display

## Hardware Requirements

- Raspberry Pi Zero W
- MicroSD card 
- ESP32 development board
- DHT22 temperature and humidity sensor
- HDMI display
- USB-TTL serial cable to access to the RPi terminal without using ecternal keyboards or hdmi
- 5/3.3V power supply

## Features

- Minimal Linux system optimized for embedded applications
- LVGL (Light and Versatile Graphics Library) for smooth GUI rendering
- WiFi connectivity 
- TCP/IP server for receiving sensor data from ESP32
- Framebuffer graphics support for direct HDMI output
- Cross-compilation SDK for efficient application development
- 
---

## Quick Start

### Building the Distribution

#### Prerequisites

```bash
# Install required packages (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install git build-essential ncurses-dev wget cpio unzip rsync bc
```

#### Build Steps

```bash
# Clone this repository
git clone https://github.com/SalimBelkhir/Buildroot-distro-for-Dashboard-applications.git
cd Buildroot-distro-for-Dashboard-applications

# Load the configuration
#Ensure writing these commands in your Buildroot directory
make rpi_zero_dashboard_defconfig

# customize configuration
make menuconfig

# Build (takes 1-2 hours on first build)
make -j$(nproc)
```

#### Flashing the Image

```bash
# Insert SD card and identify device (e.g., /dev/sdX)
lsblk

# Flash the image
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress
sync

#Or just use Balena Etcher
```

---

## Configuration Guide

### 1. UART Configuration (Serial Console Access)

```bash
# Mount the boot partition
sudo mount /dev/mmcblk0p1 /mnt/boot

# Edit boot configuration
sudo nano /mnt/boot/config.txt

# Add at the end of the file:
enable_uart=1

# Save and reboot
```

**Connect via Serial Console:**

```bash
# Connect USB TTL cable to Pi Zero (TX->RX, RX->TX, GND->GND)
#DO NOT CONNECT VCC->VCC THIS WILL HARM YOUR BOARD
sudo minicom -D /dev/ttyUSB0 -b 115200
```

### 2. Network Configuration (WiFi Setup)

```bash
# Load WiFi driver
modprobe brcmfmac

# Bring up the wireless interface
ip link set wlan0 up

# Scan for available networks
iw dev wlan0 scan | grep SSID

# Create WiFi configuration file
vi /etc/wpa_supplicant.conf
```

**Add your network credentials:**

```
network={
    ssid="YOUR_SSID"
    psk="YOUR_PASSWORD"
}
```

**Connect to WiFi:**

```bash
# Start wpa_supplicant in background
wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf

# Get IP address via DHCP
# try to connect 2.4Ghz Band else you can't move forward this step
udhcpc -i wlan0

# Verify connection
ip a
ping -c 3 8.8.8.8
```

---

## Development Workflow

### Generating the Cross-Compilation SDK 

```bash
# since lvgl is not supported by buildroot we need to generate an SDK that will cross compile lvgl code for our target
# Generate SDK from Buildroot
make sdk

# Extract SDK to your development directory
mkdir ~/my-rpi-sdk
tar xf output/images/<SDK_name.tar.gz> -C ~/my-rpi-sdk

# Relocate SDK (important step)
cd ~/my-rpi-sdk/<SDK_NAME>
./relocate-sdk.sh
```

### Building LVGL Applications

```bash
# Navigate to LVGL project directory
cd lv_port_linux_frame_buffer

# Set up cross-compilation environment
export PATH=~/my-rpi-sdk/<SDK_NAME>/bin:$PATH
export CC=arm-buildroot-linux-gnueabi-gcc
export CXX=arm-buildroot-linux-gnueabi-g++

# Build the project
mkdir build && cd build
cmake ..
make -j$(nproc)

# Binary is generated at: build/bin/lvglsim
```

### Deploying to Raspberry Pi

```bash
# Make binary executable
chmod +x bin/lvglsim

# Copy to Pi
sudo mkdir rootfs # where to mount the root filesystem partition of the SDCard (mmcblk0p2)
sudo mount /dev/mcblk0p2 /mnt/rootfs
sudo cp /path/to/bin/lvglsim /mnt/rootfs/root
sudo umount /mnt/rootfs

# Run on Raspberry Pi
sudo minicom -D /dev/ttyUSBx -b 115200
# replace x with the right number (mainly 0) you can check ls /dev/ttyUSB*
/root/lvglsim
```

---

## ESP32 Integration

### ESP32 Configuration (Client)

```bash
# copy the content of sensor-client to a new ESP-IDF client

idf.py menuconfig

# Set the following:
# - IP address of RPi
# - Wifi SSID and password
# - Server port number

# Build, flash, and monitor
idf.py build
idf.py flash
idf.py monitor
```

### Running the Server on Raspberry Pi

```bash
# On Raspberry Pi Zero terminal
./server_code

# The server will:
# 1. Listen for incoming TCP connections from ESP32
# 2. Receive temperature and humidity data
# 3. Display real-time data on LVGL GUI via HDMI
```

---



---

## Troubleshooting

### WiFi not working
```bash
# Check if module is loaded
lsmod | grep brcmfmac

# Check dmesg for errors
dmesg | grep brcm

# Manually load firmware
modprobe brcmfmac
```

### Display not showing GUI
```bash
# Check framebuffer device
ls -l /dev/fb0

# Test with simple color fill
cat /dev/urandom > /dev/fb0
```

### Serial console not responding
- Verify TX/RX connections (they should be crossed)
- Check baud rate (should be 115200)
- Ensure `enable_uart=1` is in config.txt

---



## Images

[Dashboard](https://github.com/SalimBelkhir/Buildroot-distro-for-Dashboard-applications/blob/main/images/dashboard.jpg)
[Dashvoard_display](https://github.com/SalimBelkhir/Buildroot-distro-for-Dashboard-applications/blob/main/images/dashboard_display.jpg)

## Related Links

- [Buildroot Documentation](https://buildroot.org/docs.html)
- [LVGL Documentation](https://docs.lvgl.io/)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)

---

**Note**: This is an educational IoT project. For production use, implement proper security measures, error handling, and data validation.
```

---

You can copy this directly into your `README.md` file! Would you like me to adjust anything? 📝
