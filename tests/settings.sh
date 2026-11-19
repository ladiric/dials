#! /bin/sh
# Check that the settings file is read, and that lines it cannot
# understand are reported with their line number and then ignored.
dir=tests/settings.d
rm -rf "$dir"
mkdir -p "$dir/dials" || exit 1
XDG_CONFIG_HOME=$(cd "$dir" && pwd); export XDG_CONFIG_HOME
conf=$XDG_CONFIG_HOME/dials/dials.conf

cat > "$conf" <<'END'
# A comment, and a blank line.

interval = 2.5
theme = dark
left-page = gpu
END
err=$("$DIALS" --demo --once 2>&1 >/dev/null) || exit 1
if test -n "$err"; then
  echo "a correct settings file gave messages: $err" >&2
  exit 1
fi

cat > "$conf" <<'END'
interval = 2.5
colour = blue
theme = no-such-theme
interval = 0
END
err=$("$DIALS" --demo --once 2>&1 >/dev/null) || exit 1
for line in 2 3 4; do
  case $err in
    *"dials.conf:$line: setting not understood"*) ;;
    *) echo "no message for line $line; got: $err" >&2; exit 1 ;;
  esac
done
case $err in
  *"dials.conf:1:"*) echo "line 1 is correct, but: $err" >&2; exit 1 ;;
esac
rm -rf "$dir"
