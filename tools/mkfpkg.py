#!/usr/bin/env python3
"""
Create FPKG container (dnx RTOS romfs image) from a directory tree.

Container layout (little endian):
    0x00            "FPKG\\n" + padding to 64 bytes
    0x40            u32 total file size
    0x44            [descriptor 32 B][name][content]   (each aligned to 4 B)
    ...             file entries, 16 B each            (aligned to 4 B)
    EOF-24          container header, 20 B
    EOF-4           u32 CRC32 of entire file

File names are absolute paths ("/dir/file"), UTF-8 (raw bytes on POSIX), and
entries are sorted by name bytes (FLAG_SORTED), so romfs can use binary search.

NOTE: -e is an obfuscation only. All seeds are stored in the container and no
      key is needed to decode it.
"""

import argparse
import binascii
import os
import secrets
import sys
from struct import pack

FILE_DESCRIPTOR_MAGIC  = 0x43534446
FILE_ENTRY_MAGIC       = 0x544E4546
CONTAINER_HEADER_MAGIC = 0x474B5046
HEADER_SIZE            = 64
ALIGN                  = 4

FLAG_ENCRYPTED         = 1 << 0
FLAG_SORTED            = 1 << 1

MAX_NAME_LEN           = 510            # romfs: NAME_BUF_LEN (512) - '/' - '\0'
U32_MASK               = 0xFFFFFFFF


def crc32(data):
    return binascii.crc32(data) & U32_MASK


def prng(seed):
    return (1103515245 * seed + 12345) & U32_MASK


def xcrypt(data, seed):
    out = bytearray(len(data))
    for i, byte in enumerate(data):
        seed = prng(seed)
        out[i] = byte ^ (seed & 0xFF)
    return bytes(out)


def random_seed():
    # seed 0 means "not encrypted" in container header
    while True:
        seed = secrets.randbits(32)
        if seed:
            return seed


def pad_to_align(img):
    img += bytes((-len(img)) % ALIGN)


def collect_files(root, exclude):
    files = []

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()

        if not dirnames and not filenames and os.path.abspath(dirpath) != os.path.abspath(root):
            print('warning: empty directory skipped (not representable): ' + dirpath, file=sys.stderr)

        for filename in filenames:
            path = os.path.join(dirpath, filename)

            if os.path.abspath(path) == exclude:
                continue

            rel = os.path.relpath(path, root).replace(os.sep, '/')
            name = b'/' + os.fsencode(rel)

            if len(name) > MAX_NAME_LEN:
                sys.exit('error: name too long ({} > {} bytes): {}'.format(len(name), MAX_NAME_LEN, path))

            files.append((name, path))

    # byte order, the same as strcmp() in romfs
    files.sort(key=lambda f: f[0])

    return files


def make_image(files, encrypted):
    desc_seed = random_seed() if encrypted else 0
    flags = FLAG_SORTED | (FLAG_ENCRYPTED if encrypted else 0)
    desc_flags = FLAG_ENCRYPTED if encrypted else 0

    img = bytearray(b'FPKG\n')
    img += bytes(HEADER_SIZE - len(img))
    img += bytes(4)                                     # total size, set later

    entries = bytearray()

    for n, (name, path) in enumerate(files):
        with open(path, 'rb') as f:
            data = f.read()

        # unencrypted image is reproducible
        seed = random_seed() if encrypted else 0

        pad_to_align(img)
        address = len(img)

        desc_data = pack('<IIIIII', len(name), crc32(name), len(data), crc32(data), seed, desc_flags)
        desc = pack('<II', FILE_DESCRIPTOR_MAGIC, crc32(desc_data)) + desc_data

        entry_data = pack('<II', address, 0)
        entry = pack('<II', FILE_ENTRY_MAGIC, crc32(entry_data)) + entry_data

        if encrypted:
            desc  = xcrypt(desc, desc_seed)
            name  = xcrypt(name, seed)
            data  = xcrypt(data, seed)
            entry = xcrypt(entry, (desc_seed + n) & U32_MASK)

        img += desc + name + data
        entries += entry

    pad_to_align(img)
    img += entries

    hdr_data = pack('<III', desc_seed, len(files), flags)
    hdr_crc = crc32(hdr_data)

    if encrypted:
        hdr_data = pack('<III', desc_seed,
                        len(files) ^ prng((desc_seed + 1) & U32_MASK),
                        flags ^ prng((desc_seed + 2) & U32_MASK))

    img += pack('<II', CONTAINER_HEADER_MAGIC, hdr_crc) + hdr_data

    total_size = len(img) + 4
    if total_size > U32_MASK:
        sys.exit('error: image too big ({} bytes)'.format(total_size))

    img[HEADER_SIZE:HEADER_SIZE + 4] = pack('<I', total_size)
    img += pack('<I', crc32(img))

    return img


def main():
    parser = argparse.ArgumentParser(description='Create FPKG container (romfs image) from selected directory.')
    parser.add_argument('-e', action='store_true', help='encrypted (obfuscated) container')
    parser.add_argument('search_path', help='directory with files')
    parser.add_argument('output_file', help='output container')
    args = parser.parse_args()

    if not os.path.isdir(args.search_path):
        sys.exit('error: not a directory: ' + args.search_path)

    files = collect_files(args.search_path, os.path.abspath(args.output_file))
    img = make_image(files, args.e)

    with open(args.output_file, 'wb') as fout:
        fout.write(img)


if __name__ == '__main__':
    main()
