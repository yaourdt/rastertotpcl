# TPCL Printer Application - Debian Repository

## Installation

Add the repository and install:

```bash
# Add GPG key
curl -fsSL https://yaourdt.github.io/rastertotpcl/KEY.gpg | sudo gpg --dearmor -o /usr/share/keyrings/tpcl-printer-app.gpg

# Add repository
echo "deb [signed-by=/usr/share/keyrings/tpcl-printer-app.gpg] https://yaourdt.github.io/rastertotpcl stable main" | sudo tee /etc/apt/sources.list.d/tpcl-printer-app.list

# Update and install
sudo apt update
sudo apt install tpcl-printer-app
```

## Manual Installation

Download the .deb file directly:
```bash
curl -LO https://yaourdt.github.io/rastertotpcl/pool/main/t/tpcl-printer-app/tpcl-printer-app_<version>_amd64.deb
sudo dpkg -i tpcl-printer-app_<version>_amd64.deb
sudo apt-get install -f
```
