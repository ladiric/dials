#! /bin/sh
# Check the plain-text report against a copy of /proc and /sys whose
# values are known.  The Kernel line is left out: its architecture
# comes from the machine running the test.
root=${srcdir-.}/tests/root
out=$("$DIALS" --sysroot="$root" --once) || exit 1
got=$(printf '%s\n' "$out" | grep -v '^Kernel:')
want='Host:         fixture-host
System:       Test Linux 1.0
Processor:    Test CPU Model 4-Core
CPUs:         4
Uptime:       1 day, 02:03
Load:         0.50 0.40 0.30
CPU:          n/a
Frequency:    2.60 GHz average, 4.00 GHz maximum
Temperature:  55C
Memory:       6.1 / 15.6 GiB (39%), 9.5 GiB available
Swap:         1.1 / 7.8 GiB (15%)
GPU:          GPUs are not read from a copy of /proc and /sys
Disk nvme0n1: Test NVMe Drive, 512 GB, solid state, 41C
Disk sda:     Test Hard Disk, 2.0 TB, hard disk, 33C
/:            ext4, /dev/nvme0n1p2
/mnt/my disk: xfs, /dev/sda1'
if test "$got" != "$want"; then
  echo "unexpected report; wanted:" >&2
  printf '%s\n' "$want" >&2
  echo "got:" >&2
  printf '%s\n' "$got" >&2
  exit 1
fi
