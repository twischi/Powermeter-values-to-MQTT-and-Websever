#!/bin/bash
# filepath: clean_all.sh

clear
echo
echo "Clean ALL file that has been created by 'idf.py build' "
echo
# Check if IDF_PATH is set
#
if [ -z "$IDF_PATH" ]; then
    echo "Error: IDF_PATH is not set >> RUN this script from ESP-IDF Terminal"
    echo
    exit 1
fi 

idf.py fullclean
echo "> Done 'idf.py fullclean'"
rm -f dependencies.lock
echo "> Deleted: 'dependencies.lock'" 
rm -f sdkconfig
echo "> Deleted: 'sdkconfig'"
rm -fr manaaged_components
rm -fr build
echo 
echo "Cleaned All to virgin state."
echo 
echo 
echo "If you are using VSCode ESP-IDF extension:"
echo "   (1) Do a FULL-CLEAN in the status bar"
echo "   (2) Choose a Project Configuration from status bar"
echo "   (3) Build the project again"
echo "   (4) Set the Serial-port before flashing"
echo