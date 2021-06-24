#!/bin/bash

if [ -z "$1" ]
	then
		echo "You must specify the path to the dump file to cleanup."
		exit 1
fi

dumpFile = $1

#Create a backup copy if things get messed up
cp "${dumpFile}" $(date +"%Y_%m_%d_%I_%M_%p").txt

#Remove Cobalt Strikes received output: strings from miniDump
sed -i -e 's/received output://g' "${dumpFile}"

#Trim whitespaces from miniDump
cat "${dumpFile}" | tr -d " \t\n\r" > "temp_${dumpFile}"

#Base64 decode miniDump file and create final miniDump file 
base64 -d "temp_${dumpFile}" > $(date +"%Y_%m_%d_%I_%M_%p").dmp

#Remove extra files
rm "${dumpFile}"
rm "temp_${dumpFile}"

