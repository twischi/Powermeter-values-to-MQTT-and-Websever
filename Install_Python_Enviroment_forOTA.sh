#!/bin/sh

# INSTALL a new Python virtual environment
# with required dependencies for the OTA update script

# Create a new Python virtual environment and activate it, when it doesn't exist
if [ ! -d "venv" ]; then
    python3 -m venv venv
fi

# Activate the virtual environment
. venv/bin/activate

# Install the required dependencies for the OTA update script
pip3 install -r ota_update/requirements.txt

deactivate