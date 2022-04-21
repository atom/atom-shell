#!/bin/sh

set -o errexit

rm -f /tmp/groot-says
rm -f /tmp/groot
rm -f ~/.local/share/applications/groot.desktop 

update-desktop-database ~/.local/share/applications

rm -f ~/.local/share/mime/packages/groot.xml 

update-mime-database ~/.local/share/mime
