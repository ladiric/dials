#! /bin/sh
# Check that --version prints the version line the GNU standards ask for.
out=$("$DIALS" --version) || exit 1
case $out in
  "dials (Dials) "*) exit 0 ;;
esac
echo "unexpected --version output: $out" >&2
exit 1
