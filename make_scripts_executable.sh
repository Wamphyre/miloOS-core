#!/bin/bash
# Script to make all fixed scripts executable

echo "Making fixed scripts executable..."

chmod +x core_install_fixed.sh
chmod +x resources/install_resources_fixed.sh
chmod +x configurations/apply_fixed.sh

echo "Done! You can now run:"
echo "  ./core_install_fixed.sh install"
