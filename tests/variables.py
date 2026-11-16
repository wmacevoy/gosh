# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""The variables of gosh's own that a test sets, under both names: gosh reads GOSH_ (REQ6/NAME-1),
and pf.7, which the same tests drive as the reference, reads MOSH_. mosh's own variables (MOSH_KEY,
MOSH_PREDICTION_DISPLAY, ...) have one name, and are not touched."""

GOSH_OWN = ("RELAY", "RELAY_COMMAND", "RELAY_LOG", "RELAY_STREAM_IDLE_MS", "PUNCH", "STUN", "MEETUP",
            "MEETUP_TICKET_COMMAND")


def both(env):
    """env, with each of gosh's own MOSH_ variables in it also under its GOSH_ name; a new dict."""
    env = dict(env)
    for name in GOSH_OWN:
        if "MOSH_" + name in env:
            env["GOSH_" + name] = env["MOSH_" + name]
    return env
