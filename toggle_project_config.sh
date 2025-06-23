#!/bin/bash
# filepath: toggle_project_config.sh

#
# What is the script for?
# -----------------------
#
# > This script offers a way to simply toogle between ON/OFF of the Project Conf. Editor
#
# The ESP-IDF Extesnion for VSCode offers the 'Project Configuration Editor'
# 
#     https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/additionalfeatures/project-configuration.html
#
# This Project is usting the Project Configuration Editor

clear 
echo 
echo "Toogle ON/OFF Project configurations."
echo

FILE="esp_idf_project_configuration.json"
OFF_FILE="OFF-esp_idf_project_configuration.json"

#rm -rf build
#echo "Build directory removed."
#rm -rf managed_components
#echo "Managed components directory removed."

if [ -f "$FILE" ]; then

    mv "$FILE" "$OFF_FILE"
    echo "Renamed $FILE to $OFF_FILE"
    echo "Running idf.py build silent, takes a while..."
    idf.py build > /dev/null 2>&1
    echo
    echo "Project Configuration Editor: SWITCHED OFF now."
    echo
    echo

elif [ -f "$OFF_FILE" ]; then

    mv "$OFF_FILE" "$FILE"
    echo "Renamed $OFF_FILE to $FILE"
    echo "Running clean_all.sh: What is a complete reset."
    ./clean_all.sh
    echo
    echo "Project Configuration Editor: SWITCHED ON now."
    echo ">> You have to build the project AFTER selected a Project-Config."
    echo 
    echo

else
    echo "Neither $FILE nor $OFF_FILE found."
fi