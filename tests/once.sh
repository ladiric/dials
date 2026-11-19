#! /bin/sh
# Check the plain-text report, with simulated and with real data.
out=$("$DIALS" --demo --once) || exit 1
for want in 'Host:' 'Memory:' 'Demo CPU'; do
  case $out in
    *"$want"*) ;;
    *) echo "--demo --once output lacks '$want':" >&2
       echo "$out" >&2; exit 1 ;;
  esac
done
if test -r /proc/stat; then
  "$DIALS" --once >/dev/null || exit 1
fi
