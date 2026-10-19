#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# rm9x4.sh <98|me> <tag> [boot-wait]: sections F, G and H of rm9x2.sh (the set the 2026-10-16
# requalification ran as rm9x4.sh; F is the ordered one rm9x5.sh introduced). One copy of the
# sections lives in rm9x2.sh; SECTIONS overrides the default here too.
SECTIONS="${SECTIONS:-F G H}" exec "$(dirname "${BASH_SOURCE[0]}")/rm9x2.sh" "$@"
