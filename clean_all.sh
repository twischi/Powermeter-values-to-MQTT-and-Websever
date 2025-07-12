#!/bin/bash
# filepath: clean_all.sh

clear
echo
echo "Clean ALL file that has been created by 'idf.py build' "
echo 

rm -f dependencies.lock
echo "> Deleted: 'dependencies.lock'" 
rm -f sdkconfig
echo "> Deleted: 'sdkconfig'"
"$IDF_PATH/tools/idf.py" fullclean
echo "> Done 'idf.py fullclean'"
echo 
echo "Cleaned All to virgin state."
echo 
echo 
echo "If you are using VSCode ESP-IDF extension:"
echo "   (1) QUIT VSCode and RESTART it"
echo "   (2) Choose a Project Configuration from status bar"
echo "   (3) Build the project again"
echo "   (4) Set the Serial-port before flashing"
echo