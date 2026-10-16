#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Parse the four INI templates the BcosBuilder generators load with ConfigParser, with the
# generators' own settings.
#
# The generators use comment_prefixes='/' and allow_no_value=True so that ';' comment lines are
# kept and copied into the generated configs. The price: each ';' line is an option keyed by its
# text before the first ':' or '=', so two comments sharing that text raise DuplicateOptionError
# and stop Pro config generation (the #5320 failure). This check fails the PR instead.
import configparser
import pathlib
import sys

TPL_DIR = pathlib.Path(__file__).resolve().parent.parent / "BcosBuilder" / "src" / "tpl"
TEMPLATES = ["config.genesis", "config.ini.gateway", "config.ini.node", "config.ini.rpc"]


def main():
    failed = False
    for name in TEMPLATES:
        tpl = TPL_DIR / name
        parser = configparser.ConfigParser(comment_prefixes='/', allow_no_value=True)
        try:
            with open(tpl) as f:
                parser.read_file(f)
        except (OSError, configparser.Error) as e:
            print(f"tools/BcosBuilder/src/tpl/{name}: {e}", file=sys.stderr)
            failed = True
    if failed:
        return 1
    print(f"{len(TEMPLATES)} BcosBuilder templates parse with the generator settings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
