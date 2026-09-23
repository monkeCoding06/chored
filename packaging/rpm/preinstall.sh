#!/bin/bash
# Pre-install script for chored RPM package

# Create chored group if it doesn't exist
if ! getent group chored >/dev/null; then
    groupadd -r chored
fi

# Create chored user if it doesn't exist
if ! getent passwd chored >/dev/null; then
    useradd -r -g chored -d /var/lib/chored -s /sbin/nologin \
        -c "Chored task scheduler user" chored
fi

exit 0
