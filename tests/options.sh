#! /bin/sh
# Check that bad options and operands are rejected.
fail=0
for args in '--interval=0' '--interval=abc' '--page=nosuch' \
            '--theme=nosuch' '--no-such-option' 'operand'; do
  if "$DIALS" $args --once >/dev/null 2>&1; then
    echo "accepted bad arguments: $args" >&2
    fail=1
  fi
done
exit $fail
