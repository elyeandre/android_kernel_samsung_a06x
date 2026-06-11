#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generates a GKI boot certificate by signing the boot image with avbtool."""

import subprocess


def generate_gki_certificate(image, avbtool, name, algorithm, key, salt,
                              additional_avb_args, output):
    """Generates a GKI boot certificate.

    Uses avbtool to add a hash footer and write the vbmeta image to |output|.
    This is appended to the boot image as the boot signature for GKI builds.
    """
    cmd = [
        avbtool, 'add_hash_footer',
        '--image', image,
        '--partition_name', name,
        '--algorithm', algorithm,
        '--key', key,
        '--salt', salt,
        '--output_vbmeta_image', output,
    ]
    cmd.extend(additional_avb_args)
    subprocess.check_call(cmd)
