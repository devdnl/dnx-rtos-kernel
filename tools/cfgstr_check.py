#!/usr/bin/env python3
"""
Check of driver configuration string documentation: keys parsed by the driver
(sys_opt_get_*(opts, "<key>", ...) in src/drivers/<module>/**/*.c) must be
described in the table of section "Configuration string" (subsection
drv-<module>-ddesc-cfgstr, rows "| `<key>` | ...") of src/drivers/<module>/
<module>_ioctl.h, and the table must not contain keys that the driver does not
parse. Checked are modules that define request IOCTL_<MODULE>__CONFIGURE_STR.

Usage: tools/cfgstr_check.py (from kernel directory or any other)
Exit status: 0 documentation is complete, 1 errors found.
"""

import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "drivers")

KEY_RE = re.compile(r'sys_opt_get_\w+\(\s*[^,()]+,\s*"([^"]+)"')
ROW_RE = re.compile(r'^\|\s*`([^`]+)`\s*\|', re.M)


def check(module):
    header = os.path.relpath(os.path.join(ROOT, module, module + "_ioctl.h"))
    hdr = open(header, encoding="utf-8").read()
    if ("IOCTL_%s__CONFIGURE_STR" % module.upper()) not in hdr:
        return []

    errors = []
    tag = "drv-%s-ddesc-cfgstr" % module
    m = re.search(r"\\subsection %s\b(.*?)(?=\\subsection|\n@\{|\*/)" % re.escape(tag), hdr, re.S)
    if not m:
        return ["%s: section '\\subsection %s Configuration string' not found" % (header, tag)]

    documented = set(ROW_RE.findall(m.group(1)))

    parsed = set()
    for src in glob.glob(os.path.join(ROOT, module, "**", "*.c"), recursive=True):
        parsed |= set(KEY_RE.findall(open(src, encoding="utf-8", errors="replace").read()))

    for key in sorted(parsed - documented):
        errors.append("%s: key '%s' is parsed by the driver but not documented" % (header, key))
    for key in sorted(documented - parsed):
        errors.append("%s: key '%s' is documented but not parsed by the driver" % (header, key))

    return errors


def main():
    errors = []
    modules = sorted(d for d in os.listdir(ROOT) if os.path.isfile(os.path.join(ROOT, d, d + "_ioctl.h")))
    for module in modules:
        errors += check(module)

    for e in errors:
        print(e)

    if errors:
        print("Configuration strings: %d error(s)" % len(errors))
        return 1

    print("Configuration strings: documentation of all drivers is complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
