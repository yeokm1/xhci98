#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
# rm9x5.sh <98|me> <tag> [boot-wait]: section F of rm9x2.sh (shutdown episode then remove, each
# ShnPrime value set before the load that reads it). One copy of the sections lives in rm9x2.sh.
SECTIONS="${SECTIONS:-F}" exec "$(dirname "${BASH_SOURCE[0]}")/rm9x2.sh" "$@"
