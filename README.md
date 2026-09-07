# chored

A Linux background task runner configured with TOML. Daily scheduled tasks run
on one worker thread. A local control socket lets another invocation list the
currently running task without loading configuration or starting a scheduler.

## Build

Requires Linux, Git, CMake 3.20+, and a C++17 compiler. A fresh configure fetches
pinned toml++ v3.4.0 from GitHub.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCHORED_SERVICE_USER="$(id -un)"
cmake --build build -j
```

## Run directly

```sh
./build/chored --daemon --config config/chored.toml
```

In another terminal, as the same user:

```sh
./build/chored --list-active
```

Example output (PID is the shell/process-group leader):

```text
TASK           PID     RUNNING FOR
"backup"       6124    2m 13s
```

When nothing is running, the client prints `No active tasks.` and exits zero.
A missing daemon or failed request exits nonzero with an error. Task names are
quoted, control characters are replaced, and long names are truncated for display.
Only running tasks are shown; queued tasks are excluded. The runner currently
executes at most one task at a time. The snapshot may change immediately after
it is read.

For direct runs, the default socket is `$XDG_RUNTIME_DIR/chored/control.sock`, falling back to
`/run/user/<uid>/chored/control.sock`. If your session has no runtime directory,
choose an absolute `--socket` path under an existing parent directory; its immediate
containing directory is created with mode 0700. Pass the same path to both commands.
Existing control directories must be owned by you and private (0700).
The socket is mode 0600 and both peers verify the other process's UID.

One daemon may own a socket at a time. A persistent `.lock` file prevents races;
it is intentionally not deleted on exit. Stale socket files from crashes are
recovered on startup while holding the lock. Use distinct socket paths only when
you intentionally want independent schedulers.

## Install a systemd system service

Run these commands from your normal account. `CHORED_SERVICE_USER` must name an
existing Linux account; that account executes the tasks even though the service
is managed with `sudo systemctl`.

```sh
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCHORED_SERVICE_USER="$(id -un)"
cmake --build build -j
sudo cmake --install build
sudo install -d -m 755 /etc/chored
sudo cp -n config/chored.toml /etc/chored/chored.toml
```

Alternatively, copy `config/chored.example.toml` if you want to start from the
example. Existing configuration is preserved. Ensure the chosen service user
can read `/etc/chored/chored.toml` and access the files and commands used by tasks.

Edit `/etc/chored/chored.toml`, then enable the service:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now chored
```

Manage it and inspect its logs:

```sh
sudo systemctl start chored
sudo systemctl stop chored
sudo systemctl restart chored
sudo systemctl status chored
sudo journalctl -u chored -f
```

Restart after editing the TOML configuration; live reload is not implemented.
The enabled system service starts at boot without requiring a user login.

Query active tasks **as the configured service user**:

```sh
chored --list-active --socket /run/chored/control.sock
```

For a different service account, use:

```sh
sudo -u SERVICE_USER /usr/local/bin/chored --list-active --socket /run/chored/control.sock
```

Replace `SERVICE_USER` with the account selected at configure time. The current
socket protocol requires both peers to have the same UID; running the query as
root will not work against a non-root daemon.

The unit explicitly uses `/etc/chored/chored.toml` and `/run/chored/control.sock`.
It runs in the service user's home directory and receives systemd's environment,
not your interactive shell's aliases or startup scripts. The daemon stays in the
foreground for systemd to supervise it.

With the default installation prefix, the binary is `/usr/local/bin/chored`, the
unit is `/usr/local/lib/systemd/system/chored.service`, and the example is in
`/usr/local/share/chored`. Override the unit destination using
`CHORED_SYSTEMD_UNIT_DIR`. CMake installation never overwrites the live config.

### Migrating from the earlier user service

Stop and disable the old user service before starting the system service:

```sh
systemctl --user disable --now chored.service
```

Also stop manually launched schedulers using the same tasks. Different socket
paths allow separate daemon instances, so leaving the old daemon running can
execute tasks twice. The old user unit can remain disabled; remove its installed
file if you no longer need it.

## Configuration

```toml
[tasks.backup]
command = "restic backup ~/Documents"
at = "22:00"
```

`at` is a daily local-time schedule in HH:MM format. Commands use `/bin/sh -c`.
Tasks without `at` are not scheduled. The default configuration path for direct
runs is `$XDG_CONFIG_HOME/chored/chored.toml` or `~/.config/chored/chored.toml`;
`--config` overrides it. The installed system service explicitly overrides this
with `/etc/chored/chored.toml`. Commands inherit the daemon's environment and working
directory, which can differ from your interactive terminal.

## Shutdown and current limits

Direct SIGINT/SIGTERM requests stop scheduling and wait for the current command;
active-task queries remain available during that wait. Task cancellation is not
yet implemented in TaskRunner. Under the provided systemd unit, `KillMode=control-group`
sends SIGTERM to the daemon and its jobs; systemd forces termination after 15 seconds
if necessary. Detached background processes in commands are not tracked as active
after their shell exits. Idle conditions and persistent scheduling are not implemented.

## Verification

```sh
python3 tests/control_integration.py ./build/chored
```

This uses a private temporary socket and a scheduled `sleep 8` job. It checks
empty/active/completed status, live PID, independent client mode, conflicting CLI
modes, duplicate daemon protection, malformed requests, shutdown with a silent
client, and stale socket recovery. It can take about 75 seconds because schedules
have minute precision. Run on Linux where Unix sockets are allowed.

Implementation verification: clean CMake/GCC build and standalone runner snapshot
checks passed. The authoring environment prohibited AF_UNIX socket creation, so
the full socket integration test and live systemd activation could not be run there.

References: [Unix sockets](https://man7.org/linux/man-pages/man7/unix.7.html),
[systemd execution settings](https://www.freedesktop.org/software/systemd/man/systemd.exec.html).
