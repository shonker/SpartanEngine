#!/bin/bash

# Deduce OS we are running on
is_windows=false
if [ "$1" != "gmake2" ]; then
    is_windows=true
fi

# Extract third-party libraries (that the project will link to)
echo
echo "= 1. Extracting third-party dependencies... ==========================================="
if [ "$is_windows" = false ]; then
	 Scripts/7za e ThirdParty/libraries/libraries.7z -oThirdParty/libraries/ -aoa
else 
	 Scripts/7z.exe e ThirdParty/libraries/libraries.7z -oThirdParty/libraries/ -aoa
fi
echo "======================================================================================="

# Copy engine data to the binary directory
echo
echo "2. Copying required data to the binary directory..."
mkdir -p Binaries/
cp -r Data Binaries

# Copy engine DLLs to the binary directory
echo
echo "3. Copying required DLLs to the binary directory..."
cp ThirdParty/libraries/dxcompiler.dll Binaries/
cp ThirdParty/libraries/fmod64.dll Binaries/
cp ThirdParty/libraries/fmodL64.dll Binaries/

# Generate project files
echo
if [ "$is_windows" = false ]; then
	echo "4. Generating MakeFiles..."
else
	echo "4. Generating Visual Studio solution..."
fi
echo
Scripts/premake5 --file=Scripts/premake.lua $@