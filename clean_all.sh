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