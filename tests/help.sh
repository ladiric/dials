#! /bin/sh
# Check that --help documents the options and exits successfully.
out=$("$DIALS" --help) || exit 1
for want in 'Usage: dials' '--interval' '--demo' '--once' '--version'; do
  case $out in
    *"$want"*) ;;
    *) echo "--help does not mention '$want'" >&2; exit 1 ;;
  esac
done
