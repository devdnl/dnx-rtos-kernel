#!/usr/bin/env python3
#
# dnx RTOS licenses of build: components (LICENSE.spdx files) of the compiled
# sources, SBOM (SPDX 2.3 tag-value) and notices with license texts (gzip).
#
# Component file LICENSE.spdx (in directory of component): packages separated
# by an empty line, fields as in SPDX:
#
#   PackageName: lwIP                              (required)
#   PackageVersion: 2.2.0                          (@VERSION@: VERSION file of
#                                                   the component directory)
#   PackageDownloadLocation: https://...           (default NOASSERTION)
#   PackageLicenseDeclared: BSD-3-Clause           (required, SPDX expression)
#   PackageCopyrightText: Copyright (c) ...        (may be repeated)
#   PackageComment: ...                            (optional)
#   FilesMatch: src/ext4_extent.c *.S !*_def.h     (optional, not SPDX: glob
#                                                   patterns relative to the
#                                                   directory, "!" excludes,
#                                                   package of the matching
#                                                   files only)
#
# File belongs to the first package (from its directory up) that matches it
# (FilesMatch) or has no FilesMatch. Text of license <id> is LICENSES/<id>.txt
# of the component directory or of its parent directories.
#
# Usage:
#   licenses.py files -o <out.files> [-b <base>] <objects.list>
#       source files of linked objects (dependency files *.d of objects),
#       paths of objects.list are relative to base (default: current dir)
#   licenses.py report -o <prefix> [-n <name>] [-r <root>]... <files>...
#       <prefix>.spdx (SBOM) and <prefix>-licenses.txt.gz (notices, license
#       texts); files under roots (-r) without component are reported as
#       warnings, other files without component (e.g. toolchain) are ignored
#

import argparse
import datetime
import fnmatch
import hashlib
import os
import re
import subprocess
import sys
import zlib

COMPONENT_FILE = "LICENSE.spdx"
LICENSES_DIR = "LICENSES"
GZIP_WINDOW_BITS = 12  # common_apps/licenses


# --------------------------------------------------------------------------------------------------
# files: sources of linked objects
# --------------------------------------------------------------------------------------------------
def dep_sources(dep_file, base):
    with open(dep_file, errors="replace") as f:
        text = f.read().replace("\\\n", " ")

    rule = text.split("\n", 1)[0]
    if ":" not in rule:
        return []

    deps = rule.split(":", 1)[1]
    deps = re.split(r"(?<!\\)\s+", deps.strip())
    return [os.path.normpath(os.path.join(base, d.replace("\\ ", " "))) for d in deps if d]


def cmd_files(args):
    base = os.path.abspath(args.base)
    sources = set()

    with open(args.objects) as f:
        objects = f.read().split()

    for obj in objects:
        dep = os.path.join(base, os.path.splitext(obj)[0] + ".d")
        if os.path.isfile(dep):
            sources.update(dep_sources(dep, base))
        else:
            src = os.path.join(base, obj)
            print("licenses: no dependency file of %s" % src, file=sys.stderr)

    with open(args.output, "w") as f:
        f.write("\n".join(sorted(sources)) + "\n")


# --------------------------------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------------------------------
class Package:
    def __init__(self, fields, directory):
        self.directory = directory
        self.name = fields.get("PackageName", [""])[0]
        self.version = fields.get("PackageVersion", [""])[0]
        self.download = fields.get("PackageDownloadLocation", ["NOASSERTION"])[0]
        self.license = fields.get("PackageLicenseDeclared", [""])[0]
        self.copyright = fields.get("PackageCopyrightText", [])
        self.comment = fields.get("PackageComment", [""])[0]
        self.match = " ".join(fields.get("FilesMatch", [])).split()
        self.files = 0

        if self.version == "@VERSION@":
            self.version = read_file(os.path.join(directory, "VERSION")).strip()
            commit = git_commit(directory)
            if commit:
                self.comment = (self.comment + "; " if self.comment else "") + "commit " + commit

        if not self.name or not self.license:
            raise ValueError("%s: PackageName and PackageLicenseDeclared are required"
                             % os.path.join(directory, COMPONENT_FILE))

    def matches(self, rel):
        pos = [p for p in self.match if not p.startswith("!")]
        neg = [p[1:] for p in self.match if p.startswith("!")]
        return any(fnmatch.fnmatch(rel, p) for p in pos) and not any(fnmatch.fnmatch(rel, p) for p in neg)

    def key(self):
        return (self.name, self.version, self.license)


def read_file(path):
    try:
        with open(path, errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def git_commit(directory):
    try:
        out = subprocess.run(["git", "-C", directory, "rev-parse", "--short", "HEAD"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        return out.stdout.strip() if out.returncode == 0 else ""
    except OSError:
        return ""


def parse_component_file(directory):
    packages, fields = [], {}
    for line in read_file(os.path.join(directory, COMPONENT_FILE)).splitlines() + [""]:
        line = line.rstrip()
        if line.startswith("#"):
            continue
        if not line:
            if fields:
                packages.append(Package(fields, directory))
                fields = {}
            continue
        key, _, value = line.partition(":")
        fields.setdefault(key.strip(), []).append(value.strip())
    return packages


class Components:
    def __init__(self):
        self.cache = {}

    def packages(self, directory):
        if directory not in self.cache:
            path = os.path.join(directory, COMPONENT_FILE)
            self.cache[directory] = parse_component_file(directory) if os.path.isfile(path) else None
        return self.cache[directory]

    def find(self, path):
        directory = os.path.dirname(path)
        while True:
            packages = self.packages(directory)
            if packages:
                rel = os.path.relpath(path, directory)
                for p in packages:
                    if p.matches(rel):
                        return p
                for p in packages:
                    if not p.match:
                        return p
            parent = os.path.dirname(directory)
            if parent == directory:
                return None
            directory = parent


def license_ids(expression):
    return [t for t in re.split(r"[\s()]+", expression) if t and t not in ("AND", "OR", "WITH")]


def license_text(lic, directory):
    while True:
        path = os.path.join(directory, LICENSES_DIR, lic + ".txt")
        if os.path.isfile(path):
            return read_file(path)
        parent = os.path.dirname(directory)
        if parent == directory:
            return None
        directory = parent


def spdx_id(text):
    return re.sub(r"[^A-Za-z0-9.\-]", "-", text)


def spdx_text(text):
    return "<text>" + text.replace("</text>", "< /text>") + "</text>"


def cmd_report(args):
    roots = [os.path.abspath(r) for r in args.root]
    components = Components()
    packages = {}
    missing = {}

    files = set()
    for list_file in args.files:
        files.update(p for p in read_file(list_file).split("\n") if p)

    for path in sorted(files):
        pkg = components.find(path)
        if pkg:
            pkg = packages.setdefault(pkg.key(), pkg)
            pkg.files += 1
        elif any(path.startswith(r + os.sep) for r in roots):
            d = os.path.dirname(path)
            missing[d] = missing.get(d, 0) + 1

    for d in sorted(missing):
        print("licenses: WARNING: no %s for %d file(s) of %s" % (COMPONENT_FILE, missing[d], d),
              file=sys.stderr)

    pkgs = sorted(packages.values(), key=lambda p: (p.name.lower(), p.version))

    # license texts
    texts, unknown = {}, set()
    for p in pkgs:
        for lic in license_ids(p.license):
            if lic not in texts:
                text = license_text(lic, p.directory)
                if text is None:
                    unknown.add(lic)
                else:
                    texts[lic] = text

    for lic in sorted(unknown):
        print("licenses: WARNING: no text of license %s (%s/%s.txt)" % (lic, LICENSES_DIR, lic),
              file=sys.stderr)

    write_spdx(args, pkgs, texts)
    write_notices(args, pkgs, texts)


def write_spdx(args, pkgs, texts):
    digest = hashlib.sha1("\n".join("%s %s %s" % p.key() for p in pkgs).encode()).hexdigest()
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    out = ["SPDXVersion: SPDX-2.3",
           "DataLicense: CC0-1.0",
           "SPDXID: SPDXRef-DOCUMENT",
           "DocumentName: %s" % args.name,
           "DocumentNamespace: https://github.com/devdnl/dnx-rtos-workspace/spdx/%s-%s" % (spdx_id(args.name), digest),
           "Creator: Tool: dnx-rtos-licenses.py",
           "Created: %s" % now,
           ""]

    for i, p in enumerate(pkgs, 1):
        ref = "SPDXRef-Package-%u-%s" % (i, spdx_id(p.name))
        out += ["PackageName: %s" % p.name,
                "SPDXID: %s" % ref]
        if p.version:
            out.append("PackageVersion: %s" % p.version)
        out += ["PackageDownloadLocation: %s" % p.download,
                "FilesAnalyzed: false",
                "PackageLicenseConcluded: NOASSERTION",
                "PackageLicenseDeclared: %s" % p.license,
                "PackageCopyrightText: %s" % (spdx_text("\n".join(p.copyright)) if p.copyright else "NOASSERTION")]
        if p.comment:
            out.append("PackageComment: %s" % spdx_text(p.comment))
        out += ["Relationship: SPDXRef-DOCUMENT DESCRIBES %s" % ref, ""]

    for lic in sorted(texts):
        if lic.startswith("LicenseRef-"):
            out += ["LicenseID: %s" % lic,
                    "LicenseName: %s" % lic[len("LicenseRef-"):],
                    "ExtractedText: %s" % spdx_text(texts[lic].rstrip()),
                    ""]

    with open(args.output + ".spdx", "w") as f:
        f.write("\n".join(out))


def write_notices(args, pkgs, texts):
    title = "%s: open source components and licenses" % args.name
    out = [title, "=" * len(title), ""]

    for p in pkgs:
        out.append("%s%s" % (p.name, " " + p.version if p.version else ""))
        out.append("  License: %s" % p.license)
        out += ["  " + c for c in p.copyright]
        if p.download != "NOASSERTION":
            out.append("  Source: %s" % p.download)
        if p.comment:
            out.append("  Note: %s" % p.comment)
        out.append("")

    for lic in sorted(texts):
        out += ["-" * 80, "License: %s" % lic, "-" * 80, "", texts[lic].rstrip(), ""]

    data = ("\n".join(out) + "\n").encode()

    # gzip with 4 KiB window: little RAM of decompression in device (program licenses)
    z = zlib.compressobj(9, zlib.DEFLATED, 16 + GZIP_WINDOW_BITS)
    with open(args.output + "-licenses.txt.gz", "wb") as f:
        f.write(z.compress(data) + z.flush())


# --------------------------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="dnx RTOS licenses of build")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("files", help="source files of linked objects")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("-b", "--base", default=".")
    p.add_argument("objects")

    p = sub.add_parser("report", help="SBOM and notices of source files")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("-n", "--name", default="dnx RTOS")
    p.add_argument("-r", "--root", action="append", default=[])
    p.add_argument("files", nargs="+")

    args = parser.parse_args()
    try:
        cmd_files(args) if args.cmd == "files" else cmd_report(args)
    except (OSError, ValueError) as e:
        print("licenses: %s" % e, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
