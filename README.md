# chored

A Linux background task runner configured with TOML. Daily scheduled tasks run
on a configurable worker pool. A local control socket lets another invocation list the
currently running tasks without loading configuration or starting a scheduler.

## Build

Requires Linux, Git, CMake 3.20+, and a C++17 compiler. A fresh configure fetches
pinned toml++ v3.4.0 from GitHub.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCHORED_SERVICE_USER="$(id -un)"
cmake --build build -j
```

## Everyday commands

After installing and starting the system service, run these as its configured user:

```sh
chored --list
chored --list-active
chored --run backup
chored --kill-all
```

`--list` shows configured tasks and their next run times, including manual-only
tasks. `--list-active` shows jobs currently executing. `--run TASK` queues one
configured task immediately. `--kill-all` cancels current work and clears the queue.

## Run a separate development instance

The normal default is `/run/chored/control.sock`. For development without a
system service, choose a writable private path and explicitly use it in both terminals:

```sh
./build/chored --daemon --config config/chored.toml --socket "$HOME/chored-dev/control.sock"
```

In another terminal:

```sh
./build/chored --list-active --socket "$HOME/chored-dev/control.sock"
```

The daemon creates `$HOME/chored-dev` with mode 0700 if it does not exist.
This custom socket is only needed for a separate instance. Avoid using the same
task configuration in two daemons unless you intend both to execute it.

Example output (PID is the shell/process-group leader):

```text
TASK           PID     RUNNING FOR
"backup"       6124    2m 13s
```

When nothing is running, the client prints `No active tasks.` and exits zero.
A missing daemon or failed request exits nonzero with an error. Task names are
quoted, control characters are replaced, and long names are truncated for display.
Only running tasks are shown; queued tasks are excluded. The worker limit is configured using
`[scheduler].max_concurrent_tasks` (1–64, default 1). The snapshot may change immediately after
it is read.

The default socket for the daemon and all client commands is
`/run/chored/control.sock`. No `--socket` argument is needed for the installed
system service. The socket is mode 0600 and both peers verify the other
process's UID, so run client commands as the configured service account.

`--socket PATH` remains available for a separate development instance. Its
containing directory must be owned by you and private (0700). Do not launch
a second daemon on the system service's socket.

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
chored --list-active
```

For a different service account, use:

```sh
sudo -u SERVICE_USER /usr/local/bin/chored --list-active
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
[scheduler]
max_concurrent_tasks = 4

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
active-task queries remain available during that wait. Task cancellation is available through `--kill-all`. Under the provided systemd unit, `KillMode=control-group`
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

## Cancel current work

```sh
chored --kill-all
```

Run this as the configured service account, as with `--list-active`. The default is
`/run/chored/control.sock`; `--socket` optionally overrides it.
`--kill-all`, `--daemon`, `--list`, `--list-active`, and `--run` are mutually exclusive.

The daemon immediately acknowledges: `Cancellation requested; queued tasks cleared.`
Running task process groups receive SIGTERM, then SIGKILL after a five-second
grace period. Queued occurrences are discarded. The daemon stays alive and
future scheduled occurrences remain enabled. Jobs becoming due after the request
may start normally. Repeated requests also cancel newly started work, without
extending an already cancelling job's grace period.

Tasks remain listed and retain their worker slots through the grace period,
even if their shell exits earlier. This keeps the leader PID reserved until the
final group signal and avoids targeting a recycled PID/group. Descendants that
explicitly create a new session or process group escape this group-based control.
Ordinary child processes in the task's group are included.

Workers check completion every 100 ms only while running jobs; cancellation wakes
them immediately through a condition variable. Idle workers still sleep without
polling. The acknowledgement means the request was accepted, not that every
process has already exited. Kernel-blocked processes may take longer to exit.

Verification:

```sh
cmake -S . -B build -DCHORED_SERVICE_USER="$(id -un)" -DCHORED_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
python3 tests/control_integration.py ./build/chored
```

The C++ tests cover pre-spawn cancellation, repeated requests, forced termination,
leader exit, runner reuse, parallel execution, and clearing queued work. The
socket integration test additionally checks the command against a running daemon.
See [waitid/WNOWAIT](https://man7.org/linux/man-pages/man2/wait.2.html) for the
unreaped-child behavior used during cancellation.

Validation for `--kill-all`: CMake build, runner cancellation tests, and scheduler
queue/concurrency tests passed. Unix socket communication remains unverified in
the authoring environment because AF_UNIX creation is prohibited.

## Run a task immediately

```sh
chored --run backup
```

Run the client as the configured service account. It uses the existing daemon's
loaded configuration; it does not start another scheduler or read `--config`.
The response `Task queued.` means the request was accepted, not that the command
finished successfully. Check `--list-active` and the daemon logs for execution.

Manual requests use the normal FIFO queue and concurrency limit. Unknown names,
queued/running names, and requests during shutdown return a nonzero exit status
with an explanation. Manual execution leaves the next scheduled run unchanged.
If a scheduled occurrence becomes due while that task is already pending, the
existing no-overlap behavior skips that occurrence. `--kill-all` cancels manual
jobs and clears manual queued entries too.

Tasks without `at` are retained, shown as `manual` by `--list`, and can be queued:

```toml
[tasks.hello]
command = "echo Hello from a manual task"
```

```sh
chored --run hello
```

Quote task names containing spaces. Names beginning with `--` can be passed as
`--run=--name`. Only one action may be requested per invocation: `--daemon`,
`--list`, `--list-active`, `--kill-all`, or `--run`. The run request carries the
exact name (up to 65532 bytes); it never interprets that name as shell code.

Verification:

```sh
cmake -S . -B build -DCHORED_SERVICE_USER="$(id -un)" -DCHORED_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
python3 tests/manual_control.py ./build/chored
```

The scheduler test covers manual-only tasks, simultaneous duplicate requests,
queue limits, unchanged next-run times, repeat execution, and shutdown rejection.
The socket test additionally checks CLI parsing, long names, and daemon errors.

Validation for manual runs: the CMake build, scheduler tests, and CLI argument
checks passed. End-to-end socket testing could not run because the authoring
environment rejects Unix socket creation with Operation not permitted.
