#!/bin/sh

set -o errexit

cleanup () {
  exec "./after-test.sh"
}

trap cleanup HUP INT

# Install the application Groot
cp ./groot /tmp/

# Register Groot as desktop application
cp ./groot.desktop ~/.local/share/applications 

# Update database of desktop entries
update-desktop-database ~/.local/share/applications

# Associate 'application/x-groot' mime type with '*.groot' files
cp ./groot.xml ~/.local/share/mime/packages/

# update the MIME database
update-mime-database ~/.local/share/mime
