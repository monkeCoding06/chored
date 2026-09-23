#!/bin/bash
# Post-uninstall script for chored RPM package

if [ $1 -eq 0 ]; then
    # Package is being completely removed (not upgraded)
    userdel chored 2>/dev/null || true
    groupdel chored 2>/dev/null || true
    rm -f /etc/sudoers.d/chored
fi

exit 0
