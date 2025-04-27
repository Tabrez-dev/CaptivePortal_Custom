

# Captive Portal with Time Synchronization

This project implements a **custom Captive Portal** for the **ESP32** platform, using the **ESP-IDF framework**.  
It sets up a Wi-Fi access point, redirects all HTTP requests to a local webpage, and synchronizes time over the internet when needed.

## Features 🚀

- 📶 **Wi-Fi Access Point (AP mode) setup**  
- 🌐 **DNS server redirection** to capture all user traffic
- 🖥️ **Local Webpage hosting** for captive portal
- 🕒 **SNTP Time synchronization** after connecting to external networks
- 📂 **Modular Code Structure** (separate source and header files)

## Output

![i_think_its_working](https://github.com/user-attachments/assets/140de49c-f9f7-448c-b782-5f7bcf56ce44)


## Getting Started 🛠️

### Prerequisites

- ESP32 Development Board
- ESP-IDF (v4.4 or later recommended)
- VS Code or any other IDE
- Python 3.x

### Build and Flash

```bash
# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Navigate to project directory
cd captive_portal

# Build the project
idf.py build

# Flash to ESP32
idf.py -p /dev/ttyUSB0 flash monitor
```

*(Replace `/dev/ttyUSB0` with your correct COM port.)*

---

## How it Works ✨

1. The ESP32 starts as a Wi-Fi Access Point.
2. It launches a minimal DNS server that resolves all domains to its own IP.
3. It hosts a simple webpage (under `/webpage`) for users who connect.
4. After successful captive portal interaction, it can optionally synchronize time via SNTP servers.

---

## Future Improvements 🔮

- Add HTTPS support for captive portal
- Integrate external cloud time servers
- Add authentication/authorization for portal
- Provide captive portal detection compatibility for more OSes

