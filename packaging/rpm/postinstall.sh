#!/bin/bash
# Post-install script for chored RPM package

# Set ownership for the data directory
chown chored:chored /var/lib/chored

# Set permissions for sudoers file
if [ -f /etc/sudoers.d/chored ]; then
    chmod 0440 /etc/sudoers.d/chored
fi

exit 0
