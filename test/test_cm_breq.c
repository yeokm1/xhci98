/*
 * test_cm_breq.c - the pure half of the requirement lists of hot-added
 * bridges on Windows 2000 and XP (src/cm/cm_breq.c; design record 33
 * sections 5.2, 6 and 9.2, section 10.1's vectors; roadmap-usb4.md task
 * 45.12): the templates on the lists captured in tasks 44.63 and 44.64,
 * one change of every compared field refused, the worked examples of
 * section 6.5 byte for byte, the rules' edges, section 6's arithmetic
 * bounds, the BAR walk, the diagnostic's ring, and a seeded sweep of the
 * invariants.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_breq.h"
#include "../src/cm/cm_irq.h"
#include "test_harness.h"

/*
 * The captured lists, as the registry kept them (BasicConfigVector):
 * XP's closed list of the dock's upstream port 03:00.0 and 2000's of its
 * downstream port 04:02.0 (task 44.63, the evidence xp\serial-a.log and
 * w2k\serial-w.log), 2000's configured list of the upstream port after its
 * install (the same capture), XP's configured list of a re-plugged chain's
 * upstream port (task 44.64 Part A, serial-b.log), and 2000's list of the
 * ASM4242 tunnel port, which holds an interrupt.
 */
static const UCHAR bqXpClosed[616] = {
    0x68, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0xB1, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x03, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x08, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x03, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x08, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
static const UCHAR bq2kClosed[232] = {
    0xE8, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0xB1, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
static const UCHAR bq2kConfigured[328] = {
    0x48, 0x01, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0xB1, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x01, 0x00, 0xB1, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xE5,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xE5, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xCC,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x3F, 0xCC, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
static const UCHAR bqXpConfigured[232] = {
    0xE8, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x3F, 0xFE, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x3F, 0xFD, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
static const UCHAR bq2kIrq[360] = {
    0x68, 0x01, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0xB1, 0x00, 0x00, 0x00,
    0x00, 0x60, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x01, 0x00, 0xB1, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x30, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x7F, 0xFE, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x30,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCC,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x3F, 0xCC, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x81, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0B, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ---- helpers ---------------------------------------------------------- */

static ULONG get32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

#define MB 0x100000UL
#define KB 0x400UL

/* The parent registers for windows given as base and limit. */
static void par_io(ULONG *par, ULONG base, ULONG limit)
{
    par[0] = ((base >> 8) & 0xF0UL) | (((limit >> 8) & 0xF0UL) << 8);
    par[5] = 0;
}

static void par_mem(ULONG *par, ULONG base, ULONG limit)
{
    par[1] = ((base >> 16) & 0xFFF0UL) | (limit & 0xFFF00000UL);
}

static void par_pref(ULONG *par, ULONG base, ULONG limit)
{
    par[2] = ((base >> 16) & 0xFFF0UL) | (limit & 0xFFF00000UL) | 0x10001UL;
    par[3] = 0;
    par[4] = 0;
}

static void par_pref64(ULONG *par, ULONG bHi, ULONG bLo, ULONG lHi, ULONG lLo)
{
    par[2] = ((bLo >> 16) & 0xFFF0UL) | (lLo & 0xFFF00000UL) | 0x10001UL;
    par[3] = bHi;
    par[4] = lHi;
}

static void par_closed(ULONG *par, ULONG k)
{
    if (k == CM_BREQ_K_IO) {
        par[0] = 0x00F0UL;
        par[5] = 0;
    } else if (k == CM_BREQ_K_MEM) {
        par[1] = 0x0000FFF0UL;
    } else {
        par[2] = 0x0001FFF1UL;
        par[3] = 0;
        par[4] = 0;
    }
}

/* The windows of 44.63's tunnel port, which example 1's upstream port
 * then takes whole: I/O 1000h-3FFFh, memory CE000000h-E5FFFFFFh,
 * prefetchable CD200000h-CD3FFFFFh. */
static void par_tunnel(ULONG *par)
{
    par_io(par, 0x1000UL, 0x3FFFUL);
    par_mem(par, 0xCE000000UL, 0xE5FFFFFFUL);
    par_pref(par, 0xCD200000UL, 0xCD3FFFFFUL);
}

static void snap_up(CM_BREQ_SNAP *s, ULONG n)
{
    memset(s, 0, sizeof(*s));
    s->Role = CM_BREQ_ROLE_UP;
    s->ParentRead = 1;
    par_tunnel(s->Par);
    s->Alone = CM_BREQ_YES;
    s->SecNumbered = (ULONG)(n != CM_BREQ_N_UNKNOWN);
    s->N = n;
}

static void sib(CM_BREQ_SNAP *s, ULONG behind, ULONG io, ULONG pref,
                ULONG hotplug)
{
    CM_BREQ_SIB *b;

    b = &s->Sib[s->Sibs++];
    b->Dsp = 1;
    b->Numbered = (ULONG)(behind != CM_BREQ_B_UNREAD);
    b->HotPlug = hotplug;
    b->Behind = behind;
    b->AnyIo = io;
    b->AnyPref = pref;
}

static void snap_down(CM_BREQ_SNAP *s, ULONG index)
{
    memset(s, 0, sizeof(*s));
    s->Role = CM_BREQ_ROLE_DOWN;
    s->ParentRead = 1;
    par_tunnel(s->Par);
    s->Alone = CM_BREQ_NO;
    s->SibsRead = 1;
    s->Index = index;
}

/* One expected descriptor of a written list: its kind (1 I/O, 3 memory,
 * 4 prefetchable, 0x81 private), Option and Length (for a private
 * descriptor, its Data[1]). */
typedef struct _EXP {
    ULONG Option;
    ULONG Kind;
    ULONG Length;
} EXP;

#define PRIV(k) { 0, 0x81, (k) }
#define IO(opt, len) { (opt), 1, (len) }
#define MEM(opt, len) { (opt), 3, (len) }
#define PF(opt, len) { (opt), 4, (len) }
#define N_OF(a) ((ULONG)(sizeof(a) / sizeof((a)[0])))

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFF);
    p[1] = (UCHAR)((v >> 8) & 0xFF);
    p[2] = (UCHAR)((v >> 16) & 0xFF);
    p[3] = (UCHAR)((v >> 24) & 0xFF);
}

/* The 32 bytes the PCI driver's own fields and the rules make of `x` on
 * `minor` (section 2.1's captures: I/O ShareDisposition 1, flags B1h,
 * maximum FFFFh; memory ShareDisposition 0 on XP and 1 on 2000, maximum
 * FFFFFFFFh; prefetchable flags 4, maximum all ones; the rewriter's
 * alignment the granule and minimum 0). */
static void exp_desc(ULONG minor, const EXP *x, UCHAR *d)
{
    memset(d, 0, 32);
    d[0] = (UCHAR)x->Option;
    if (x->Kind == 0x81) {
        d[1] = 0x81;
        d[2] = 1;
        put32(d + 8, 1);
        put32(d + 12, x->Length);
        return;
    }
    if (x->Kind == 1) {
        d[1] = 1;
        d[2] = 1;
        d[4] = 0xB1;
        put32(d + 8, x->Length);
        put32(d + 12, 0x1000UL);
        put32(d + 24, 0xFFFFUL);
        return;
    }
    d[1] = 3;
    d[2] = (UCHAR)(minor == 1 ? 0 : 1);
    d[4] = (UCHAR)(x->Kind == 4 ? 4 : 0);
    put32(d + 8, x->Length);
    put32(d + 12, MB);
    put32(d + 24, 0xFFFFFFFFUL);
    put32(d + 28, x->Kind == 4 ? 0xFFFFFFFFUL : 0UL);
}

/* The plan for `list` on `minor` written and compared byte for byte with
 * the list `want` describes: the header the input's but ListSize and
 * Count, one alternative list. The writer refuses one byte less of room
 * and writes nothing then; at exactly its size it writes nothing past. */
static void check_written(const UCHAR *list, ULONG minor,
                          const CM_BREQ_SNAP *s, const EXP *want, ULONG n,
                          const char *what)
{
    CM_BREQ_PLAN p;
    UCHAR out[2048];
    UCHAR exp[2048];
    UCHAR canary[2048];
    ULONG got;
    ULONG i;
    ULONG bytes;
    char msg[160];

    bytes = 40UL + 32UL * n;
    memcpy(exp, list, 40);
    put32(exp, bytes);
    put32(exp + 36, n);
    for (i = 0; i < n; i++) {
        exp_desc(minor, &want[i], exp + 40 + 32 * i);
    }
    memset(out, 0xA5, sizeof(out));
    CHECK_EQ(CmBreqPlan(list, minor, s, &p), CM_BREQ_OUT_REWRITTEN, what);
    sprintf(msg, "%s: the planned size", what);
    CHECK_EQ(p.Bytes, bytes, msg);
    got = CmBreqWrite(list, &p, out, bytes);
    sprintf(msg, "%s: written at exactly its size", what);
    CHECK_EQ(got, bytes, msg);
    sprintf(msg, "%s: byte for byte", what);
    CHECK(got == bytes && memcmp(out, exp, bytes) == 0, msg);
    if (got == bytes && memcmp(out, exp, bytes) != 0) {
        for (i = 0; i < bytes; i++) {
            if (out[i] != exp[i]) {
                printf("  %s: first difference at byte %lu (desc %ld): got "
                       "%02X want %02X\n", what, i,
                       i >= 40 ? (long)((i - 40) / 32) : -1L, out[i], exp[i]);
                break;
            }
        }
    }
    sprintf(msg, "%s: nothing past the list", what);
    CHECK(out[bytes] == 0xA5, msg);
    memset(canary, 0x3C, sizeof(canary));
    memcpy(out, canary, sizeof(out));
    sprintf(msg, "%s: one byte short of room refused, nothing written", what);
    CHECK(CmBreqWrite(list, &p, out, bytes - 1) == 0 &&
              memcmp(out, canary, sizeof(out)) == 0,
          msg);
}

/* ---- the templates ---------------------------------------------------- */

static void test_templates(void)
{
    UCHAR m[sizeof(bqXpClosed)];
    ULONG b;
    ULONG bad;

    CHECK_EQ(CmBreqShape(bqXpClosed, 1), CM_BREQ_SHAPE_XP,
             "XP's closed list is XP's template");
    CHECK_EQ(CmBreqShape(bq2kClosed, 0), CM_BREQ_SHAPE_2000,
             "2000's closed list is 2000's template");
    CHECK_EQ(CmBreqShape(bqXpClosed, 0), CM_BREQ_SHAPE_NONE,
             "XP's list on 2000 is left");
    CHECK_EQ(CmBreqShape(bq2kClosed, 1), CM_BREQ_SHAPE_NONE,
             "2000's list on XP is left");
    CHECK_EQ(CmBreqShape(bqXpClosed, 2), CM_BREQ_SHAPE_XP,
             "XP x64's closed list is XP's template (task 45.55)");
    CHECK_EQ(CmBreqShape(bq2kClosed, 2), CM_BREQ_SHAPE_NONE,
             "2000's list on XP x64 is left");
    CHECK_EQ(CmBreqShape(bq2kConfigured, 0), CM_BREQ_SHAPE_NONE,
             "2000's configured list is left");
    CHECK_EQ(CmBreqShape(bqXpConfigured, 1), CM_BREQ_SHAPE_NONE,
             "XP's configured list is left");
    CHECK_EQ(CmBreqShape(bqXpConfigured, 0), CM_BREQ_SHAPE_NONE,
             "XP's configured list is left on 2000 too (its size is 2000's)");
    CHECK_EQ(CmBreqShape(bq2kIrq, 0), CM_BREQ_SHAPE_NONE,
             "a list with an interrupt is left");
    CHECK_EQ(CmBreqShape(NULL, 1), CM_BREQ_SHAPE_NONE, "no list");

    /* One change of every compared byte (all but the interface type, bus
     * and slot at 4 to 15) refuses the template. */
    bad = 0;
    for (b = 0; b < sizeof(bqXpClosed); b++) {
        if (b >= 4 && b < 16) {
            continue;
        }
        memcpy(m, bqXpClosed, sizeof(bqXpClosed));
        m[b] ^= 0x01;
        if (CmBreqShape(m, 1) != CM_BREQ_SHAPE_NONE) {
            bad++;
            printf("  XP byte %lu accepted when changed\n", b);
        }
        m[b] ^= 0x81;
        if (CmBreqShape(m, 1) != CM_BREQ_SHAPE_NONE) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0, "every XP field compared");
    bad = 0;
    for (b = 0; b < sizeof(bq2kClosed); b++) {
        if (b >= 4 && b < 16) {
            continue;
        }
        memcpy(m, bq2kClosed, sizeof(bq2kClosed));
        m[b] ^= 0x01;
        if (CmBreqShape(m, 0) != CM_BREQ_SHAPE_NONE) {
            bad++;
            printf("  2000 byte %lu accepted when changed\n", b);
        }
    }
    CHECK_EQ(bad, 0, "every 2000 field compared");
    memcpy(m, bqXpClosed, sizeof(bqXpClosed));
    m[4] ^= 0xFF;
    m[8] ^= 0xFF;
    m[12] ^= 0xFF;
    CHECK_EQ(CmBreqShape(m, 1), CM_BREQ_SHAPE_XP,
             "the interface type, bus and slot are not compared");
    /* The order of the groups: memory and prefetchable swapped. */
    memcpy(m, bqXpClosed, sizeof(bqXpClosed));
    memcpy(m + 40 + 32 * 2, bqXpClosed + 40 + 32 * 10, 32 * 8);
    memcpy(m + 40 + 32 * 10, bqXpClosed + 40 + 32 * 2, 32 * 8);
    CHECK_EQ(CmBreqShape(m, 1), CM_BREQ_SHAPE_NONE, "groups reordered");
}

/* ---- the worked examples (section 6.5) ------------------------------- */

static const EXP ex1Up[] = {
    IO(0, 12 * KB), IO(8, 8 * KB), IO(8, 4 * KB), PRIV(2),
    MEM(0, 384 * MB), MEM(8, 192 * MB), MEM(8, 96 * MB), MEM(8, 48 * MB),
    MEM(8, 24 * MB), MEM(8, 12 * MB), MEM(8, 6 * MB), MEM(8, 3 * MB),
    MEM(8, 2 * MB), MEM(8, 1 * MB), PRIV(3),
    PF(0, 2 * MB), PF(8, 1 * MB), PRIV(4)
};

static const EXP ex1Mem128[] = {
    MEM(0, 128 * MB), MEM(8, 64 * MB), MEM(8, 32 * MB), MEM(8, 16 * MB),
    MEM(8, 8 * MB), MEM(8, 4 * MB), MEM(8, 2 * MB), MEM(8, 1 * MB), PRIV(3)
};

static const EXP ex1Nic[] = {
    IO(0, 4 * KB), PRIV(2),
    MEM(0, 128 * MB), MEM(8, 64 * MB), MEM(8, 32 * MB), MEM(8, 16 * MB),
    MEM(8, 8 * MB), MEM(8, 4 * MB), MEM(8, 2 * MB), MEM(8, 1 * MB), PRIV(3)
};

static void dock3(CM_BREQ_SNAP *s, ULONG index)
{
    snap_down(s, index);
    sib(s, CM_BREQ_B_TYPE0, 0, 0, 0);   /* the NVMe   */
    sib(s, CM_BREQ_B_TYPE0, 1, 0, 0);   /* the e1000e */
    sib(s, CM_BREQ_B_TYPE0, 0, 0, 0);   /* the xHCI   */
}

static void test_example1(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;
    ULONG e[7];

    snap_up(&s, 3);
    check_written(bqXpClosed, 1, &s, ex1Up, N_OF(ex1Up), "ex1 upstream (XP)");
    check_written(bq2kClosed, 0, &s, ex1Up, N_OF(ex1Up),
                  "ex1 upstream (2000)");
    dock3(&s, 0);
    check_written(bqXpClosed, 1, &s, ex1Mem128, N_OF(ex1Mem128),
                  "ex1 the NVMe's port");
    check_written(bq2kClosed, 0, &s, ex1Mem128, N_OF(ex1Mem128),
                  "ex1 the NVMe's port (2000)");
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Dropped, 5, "ex1 the NVMe's port drops I/O and prefetchable");
    CHECK_EQ(p.Unmet, 0, "ex1 the NVMe's port has no unmet need");
    CHECK_EQ(p.Claims, 1UL << 2, "ex1 the NVMe's claims");
    dock3(&s, 1);
    check_written(bqXpClosed, 1, &s, ex1Nic, N_OF(ex1Nic),
                  "ex1 the NIC's port");
    check_written(bq2kClosed, 0, &s, ex1Nic, N_OF(ex1Nic),
                  "ex1 the NIC's port (2000)");
    dock3(&s, 2);
    check_written(bqXpClosed, 1, &s, ex1Mem128, N_OF(ex1Mem128),
                  "ex1 the xHCI's port");
    check_written(bq2kClosed, 0, &s, ex1Mem128, N_OF(ex1Mem128),
                  "ex1 the xHCI's port (2000)");
    /* The diagnostic's entry of the NIC's port. */
    dock3(&s, 1);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CmBreqEntry(&p, 0x0408UL, 0x0218UL, 1, p.Outcome, 1, e);
    CHECK_EQ(e[0], 0x02180408UL, "entry: the locations");
    CHECK_EQ(e[1], 2UL | (1UL << 4) | (1UL << 8) | (1UL << 12) |
                       (4UL << 16),
             "entry: role, shape, system, outcome, prefetchable dropped");
    CHECK_EQ(e[2], 3UL | (1UL << 8) | (2UL << 16) | (1UL << 20) |
                       (1UL << 22),
             "entry: three siblings, index 1, numbered, I/O and memory "
             "claimed");
    CHECK_EQ(e[3], 12 * KB, "entry: the parent's I/O");
    CHECK_EQ(e[4], 384UL | (2UL << 16), "entry: the parent's MiB");
    CHECK_EQ(e[5], 1UL | (1UL << 8) | (1UL << 16),
             "entry: I/O written and the OS's, rewritten once");
    CHECK_EQ(e[6], 128UL, "entry: memory written, no prefetchable");
}

static void test_example2(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;
    UCHAR out[1024];

    snap_up(&s, 1);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "ex2 the single enclosure's upstream port: left");
    CHECK_EQ(CmBreqPlan(bq2kClosed, 0, &s, &p), CM_BREQ_OUT_LEFT,
             "ex2 on 2000: left");
    snap_up(&s, 0);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "N 0: left");
    snap_down(&s, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "ex2 one sibling: left");
    CHECK_EQ(CmBreqWrite(bqXpClosed, &p, out, sizeof(out)), 0,
             "a list left is never written");
}

static const EXP ex3D1a[] = {
    MEM(0, 192 * MB), MEM(8, 96 * MB), MEM(8, 48 * MB), MEM(8, 24 * MB),
    MEM(8, 12 * MB), MEM(8, 6 * MB), MEM(8, 3 * MB), MEM(8, 1 * MB), PRIV(3)
};

static const EXP ex3D1b[] = {
    IO(0, 4 * KB), PRIV(2),
    MEM(0, 192 * MB), MEM(8, 96 * MB), MEM(8, 48 * MB), MEM(8, 24 * MB),
    MEM(8, 12 * MB), MEM(8, 6 * MB), MEM(8, 3 * MB), MEM(8, 1 * MB), PRIV(3),
    PF(0, 2 * MB), PF(8, 1 * MB), PRIV(4)
};

static void chain2(CM_BREQ_SNAP *s, ULONG index, ULONG present)
{
    snap_down(s, index);
    sib(s, CM_BREQ_B_TYPE0, 0, 0, 0);
    sib(s, present ? CM_BREQ_B_TYPE1 : CM_BREQ_B_NONE, 0, 0, 1);
}

static void test_example3(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;

    snap_up(&s, 2);
    check_written(bqXpClosed, 1, &s, ex1Up, N_OF(ex1Up), "ex3 U1");
    check_written(bq2kClosed, 0, &s, ex1Up, N_OF(ex1Up), "ex3 U1 (2000)");
    chain2(&s, 0, 0);
    check_written(bqXpClosed, 1, &s, ex3D1a, N_OF(ex3D1a), "ex3 D1a");
    check_written(bq2kClosed, 0, &s, ex3D1a, N_OF(ex3D1a), "ex3 D1a (2000)");
    chain2(&s, 1, 0);
    check_written(bqXpClosed, 1, &s, ex3D1b, N_OF(ex3D1b), "ex3 D1b");
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Claims, 2UL | (1UL << 2) | (1UL << 4), "ex3 D1b's claims");
    CHECK_EQ(p.Flags, 2UL | 4UL, "ex3 D1b: numbered, hot-plug capable");
    check_written(bq2kClosed, 0, &s, ex3D1b, N_OF(ex3D1b), "ex3 D1b (2000)");
    /* Plugged together: a type 1 function behind D1b, the same division. */
    chain2(&s, 1, 1);
    check_written(bqXpClosed, 1, &s, ex3D1b, N_OF(ex3D1b),
                  "ex3 D1b with the chain present");
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Flags, 2UL | 4UL | 8UL, "ex3 D1b: a nested bridge behind");
    /* U2 behind D1b: alone, N 1, the OS list. */
    snap_up(&s, 1);
    par_io(s.Par, 0x1000UL, 0x1FFFUL);
    par_mem(s.Par, 0xD0000000UL, 0xDBFFFFFFUL);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "ex3 U2: the OS list");
    CHECK_EQ(CmBreqPlan(bq2kClosed, 0, &s, &p), CM_BREQ_OUT_LEFT,
             "ex3 U2 on 2000: the OS list");
}

static const EXP ex4A[] = {
    MEM(0, 383 * MB), MEM(8, 191 * MB), MEM(8, 95 * MB), MEM(8, 47 * MB),
    MEM(8, 23 * MB), MEM(8, 11 * MB), MEM(8, 5 * MB), MEM(8, 2 * MB),
    MEM(8, 1 * MB), PRIV(3)
};

static const EXP ex4B[] = { MEM(0, 1 * MB), PRIV(3) };

static void test_example4(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;

    snap_down(&s, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    sib(&s, CM_BREQ_B_NONE, 0, 0, 0);
    check_written(bqXpClosed, 1, &s, ex4A, N_OF(ex4A), "ex4 the first port");
    check_written(bq2kClosed, 0, &s, ex4A, N_OF(ex4A),
                  "ex4 the first port (2000)");
    s.Index = 1;
    check_written(bqXpClosed, 1, &s, ex4B, N_OF(ex4B),
                  "ex4 the empty port: exactly 1 MB");
    check_written(bq2kClosed, 0, &s, ex4B, N_OF(ex4B),
                  "ex4 the empty port (2000)");
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Claims, 3UL << 2, "ex4 the empty port: granule only");
}

static const EXP ex5Nic[] = {
    IO(0, 4 * KB), PRIV(2),
    MEM(0, 128 * MB), MEM(8, 64 * MB), MEM(8, 32 * MB), MEM(8, 16 * MB),
    MEM(8, 8 * MB), MEM(8, 4 * MB), MEM(8, 2 * MB), MEM(8, 1 * MB), PRIV(3)
};

static const EXP ex5Pref[] = {
    MEM(0, 128 * MB), MEM(8, 64 * MB), MEM(8, 32 * MB), MEM(8, 16 * MB),
    MEM(8, 8 * MB), MEM(8, 4 * MB), MEM(8, 2 * MB), MEM(8, 1 * MB), PRIV(3),
    PF(0, 1 * MB), PRIV(4)
};

static void short5(CM_BREQ_SNAP *s, ULONG index, ULONG prefMb)
{
    snap_down(s, index);
    par_io(s->Par, 0x3000UL, 0x3FFFUL);
    par_pref(s->Par, 0xCD200000UL, 0xCD200000UL + prefMb * MB - 1UL);
    sib(s, CM_BREQ_B_TYPE0, 1, 0, 0);   /* the NIC              */
    sib(s, CM_BREQ_B_NONE, 0, 0, 1);    /* the empty chain port */
    sib(s, CM_BREQ_B_TYPE0, 0, 1, 0);   /* a prefetchable BAR   */
}

static void test_example5(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;

    short5(&s, 0, 2);
    check_written(bqXpClosed, 1, &s, ex5Nic, N_OF(ex5Nic), "ex5 the NIC");
    check_written(bq2kClosed, 0, &s, ex5Nic, N_OF(ex5Nic),
                  "ex5 the NIC (2000)");
    short5(&s, 1, 2);
    check_written(bqXpClosed, 1, &s, ex5Pref, N_OF(ex5Pref),
                  "ex5 the chain port: no I/O, 1 MB prefetchable");
    check_written(bq2kClosed, 0, &s, ex5Pref, N_OF(ex5Pref),
                  "ex5 the chain port (2000)");
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Unmet, 0, "ex5 the chain port's unknown I/O is no unmet need");
    CHECK_EQ(p.Dropped, 1, "ex5 the chain port drops I/O");
    short5(&s, 2, 2);
    check_written(bqXpClosed, 1, &s, ex5Pref, N_OF(ex5Pref),
                  "ex5 the third port");
    /* 1 MB prefetchable: the chain port, first in sibling order of the
     * same class, takes it; the third port's need is unmet. */
    short5(&s, 2, 1);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Outcome, CM_BREQ_OUT_REWRITTEN, "ex5 1 MB: rewritten");
    CHECK_EQ(p.Unmet, 4, "ex5 1 MB: the third port's prefetchable unmet");
    CHECK(!p.Keep[CM_BREQ_K_PREF], "ex5 1 MB: no prefetchable group");
    short5(&s, 1, 1);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK(p.Keep[CM_BREQ_K_PREF] && p.Len[CM_BREQ_K_PREF][0] == MB,
          "ex5 1 MB: the chain port granted it");
}

static const EXP ex6Up[] = {
    MEM(0, 64 * MB), MEM(8, 32 * MB), MEM(8, 16 * MB), MEM(8, 8 * MB),
    MEM(8, 4 * MB), MEM(8, 2 * MB), MEM(8, 1 * MB), PRIV(3),
    PF(0, 64 * MB), PF(8, 32 * MB), PF(8, 16 * MB), PF(8, 8 * MB),
    PF(8, 4 * MB), PF(8, 2 * MB), PF(8, 1 * MB), PRIV(4)
};

static void test_example6(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;

    snap_up(&s, 1);
    par_closed(s.Par, CM_BREQ_K_IO);
    check_written(bqXpClosed, 1, &s, ex6Up, N_OF(ex6Up),
                  "ex6 the single enclosure: I/O dropped, the rest as it was");
    {
        static const EXP ex6Up2k[] = {
            MEM(0, 1 * MB), PRIV(3), PF(0, 1 * MB), PRIV(4)
        };

        check_written(bq2kClosed, 0, &s, ex6Up2k, N_OF(ex6Up2k),
                      "ex6 the single enclosure (2000)");
    }
    snap_down(&s, 0);
    par_closed(s.Par, CM_BREQ_K_IO);
    sib(&s, CM_BREQ_B_TYPE0, 1, 0, 0);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Outcome, CM_BREQ_OUT_REWRITTEN, "ex6 the NIC's port");
    CHECK_EQ(p.Unmet, 1, "ex6 the NIC's I/O need unmet");
    CHECK_EQ(p.Dropped, 1, "ex6 I/O dropped");
    snap_up(&s, 3);
    par_closed(s.Par, CM_BREQ_K_IO);
    (VOID)CmBreqPlan(bq2kClosed, 0, &s, &p);
    CHECK(!p.Keep[CM_BREQ_K_IO] && p.Keep[CM_BREQ_K_MEM] &&
              p.Len[CM_BREQ_K_MEM][0] == 384 * MB,
          "ex6 a dock on 2000: no I/O, the whole memory window");
}

/* ---- the rules' edges ------------------------------------------------- */

static void test_edges(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;
    ULONG k;
    ULONG n;
    ULONG i;
    ULONG got;

    snap_up(&s, CM_BREQ_N_UNKNOWN);
    check_written(bqXpClosed, 1, &s, ex1Up, N_OF(ex1Up),
                  "N unknown is N >= 2");
    snap_up(&s, 2);
    check_written(bqXpClosed, 1, &s, ex1Up, N_OF(ex1Up), "N 2");
    snap_up(&s, 8);
    check_written(bqXpClosed, 1, &s, ex1Up, N_OF(ex1Up), "N 8");
    snap_up(&s, 3);
    s.Alone = CM_BREQ_NO;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_NOTALONE,
             "not alone: left");
    s.Alone = CM_BREQ_UNKNOWN;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_NOTALONE,
             "alone unknown: left");
    par_closed(s.Par, CM_BREQ_K_PREF);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "not alone, a kind the parent lacks: dropped");
    CHECK(p.Keep[CM_BREQ_K_IO] && !p.Sized[CM_BREQ_K_IO] &&
              !p.Keep[CM_BREQ_K_PREF],
          "not alone: the rest as it was");
    /* Every parent window 0 in turn, and all of them. */
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        snap_up(&s, 3);
        par_closed(s.Par, k);
        (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
        CHECK(p.Outcome == CM_BREQ_OUT_REWRITTEN && !p.Keep[k] &&
                  p.Dropped == (1UL << k),
              "a parent window 0: its group dropped");
    }
    snap_up(&s, 3);
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        par_closed(s.Par, k);
    }
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_NOWIN,
             "a parent with no window: left, no window");
    snap_up(&s, 1);
    for (k = 0; k < CM_BREQ_KINDS; k++) {
        par_closed(s.Par, k);
    }
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_NOWIN,
             "N 1 and no window: left");
    /* No list without a window: 1 MB of memory, two memory claimants. */
    snap_down(&s, 1);
    par_mem(s.Par, 0xE0000000UL, 0xE00FFFFFUL);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_NOWIN,
             "the second port of two in 1 MB: left, no window");
    CHECK_EQ(p.Bytes, 0, "no window: no new list");
    s.Index = 0;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "the first port of two in 1 MB: rewritten");
    CHECK(p.Keep[CM_BREQ_K_MEM] && p.Steps[CM_BREQ_K_MEM] == 1 &&
              p.Len[CM_BREQ_K_MEM][0] == MB,
          "the first port: exactly its granule");
    /* The order: a "yes" after an "unknown" in sibling order served first. */
    snap_down(&s, 1);
    par_io(s.Par, 0x3000UL, 0x3FFFUL);
    sib(&s, CM_BREQ_B_UNREAD, 0, 0, 0);
    sib(&s, CM_BREQ_B_TYPE0, 1, 0, 0);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK(p.Keep[CM_BREQ_K_IO], "a yes after an unknown gets the I/O");
    s.Index = 0;
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK(!p.Keep[CM_BREQ_K_IO] && p.Unmet == 0,
          "the unknown before it does not, and is no unmet need");
    /* Short I/O, 8 KB, with two, three and four claimants of "yes". */
    for (n = 2; n <= 4; n++) {
        got = 0;
        for (i = 0; i < n; i++) {
            snap_down(&s, i);
            par_io(s.Par, 0x2000UL, 0x3FFFUL);
            for (k = 0; k < n; k++) {
                sib(&s, CM_BREQ_B_TYPE0, 1, 0, 0);
            }
            (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
            if (p.Keep[CM_BREQ_K_IO]) {
                got++;
                CHECK(i < 2, "short I/O: the first two in sibling order");
            } else {
                CHECK_EQ(p.Unmet & 1, 1, "short I/O: the rest unmet");
            }
        }
        CHECK_EQ(got, 2, "short I/O: 8 KB serves two");
    }
    /* Short prefetchable: 2 MB, three "yes" claimants. */
    got = 0;
    for (i = 0; i < 3; i++) {
        snap_down(&s, i);
        for (k = 0; k < 3; k++) {
            sib(&s, CM_BREQ_B_TYPE0, 0, 1, 0);
        }
        (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
        if (p.Keep[CM_BREQ_K_PREF]) {
            got++;
            CHECK_EQ(p.Len[CM_BREQ_K_PREF][0], MB,
                     "short prefetchable: a granule each, nothing left");
        }
    }
    CHECK_EQ(got, 2, "short prefetchable: 2 MB serves two");
    /* An "other" function among the siblings: the OS lists but for
     * rule 6.2.1. */
    snap_down(&s, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    sib(&s, CM_BREQ_B_TYPE0, 1, 0, 0);
    s.Sib[1].Dsp = 0;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "an other function on the bus: left");
    par_closed(s.Par, CM_BREQ_K_PREF);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "an other function: rule 6.2.1 still applies");
    CHECK(p.Keep[CM_BREQ_K_MEM] && !p.Sized[CM_BREQ_K_MEM],
          "an other function: memory as the OS list asks");
    /* A bridge that is neither. */
    snap_up(&s, 3);
    s.Role = CM_BREQ_ROLE_OTHER;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "a bridge that is neither: left");
    par_closed(s.Par, CM_BREQ_K_IO);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "a bridge that is neither: rule 6.2.1");
    /* More siblings than the snapshot's room: no division. */
    snap_down(&s, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    s.Sibs = CM_BREQ_SIBS_MAX + 1;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "siblings over the room: left");
    s.Sibs = 2;
    s.Index = 2;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "an index past the siblings: left");
    /* The siblings unread: the OS list but for 6.2.1. */
    snap_down(&s, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 0, 0);
    s.SibsRead = 0;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "siblings unread: left");
    /* The parent unread, and a configured list. */
    snap_up(&s, 3);
    s.ParentRead = 0;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_PARENT,
             "the parent unread: left");
    snap_up(&s, 3);
    s.Par[0] = 0xF1F1UL;
    s.Par[5] = 0x00010001UL;
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &p), CM_BREQ_OUT_PARENT,
             "an I/O parent window above 10000h: left");
    snap_up(&s, 3);
    CHECK_EQ(CmBreqPlan(bq2kConfigured, 0, &s, &p), CM_BREQ_OUT_SHAPE,
             "a configured list: shape refused");
    CHECK_EQ(CmBreqPlan(bq2kClosed, 2, &s, &p), CM_BREQ_OUT_SHAPE,
             "2000's list on XP x64: shape refused");
    /* A prefetchable claim moved to memory where the upstream port has no
     * prefetchable window. */
    snap_down(&s, 1);
    par_closed(s.Par, CM_BREQ_K_PREF);
    sib(&s, CM_BREQ_B_NONE, 0, 0, 0);
    sib(&s, CM_BREQ_B_TYPE0, 0, 1, 0);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Unmet, 0, "a prefetchable BAR with no prefetchable window: "
                         "no unmet need");
    CHECK_EQ(p.Claims, 1UL << 2, "it counts as memory");
    CHECK(p.Keep[CM_BREQ_K_MEM] && p.Len[CM_BREQ_K_MEM][0] == 383 * MB,
          "its share: all but the empty port's granule");
    /* A nested bridge and a read failure: unknown I/O, memory and
     * prefetchable yes. */
    snap_down(&s, 0);
    sib(&s, CM_BREQ_B_TYPE1, 0, 0, 0);
    sib(&s, CM_BREQ_B_UNREAD, 0, 0, 0);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Claims, 2UL | (1UL << 2) | (1UL << 4),
             "a nested bridge's claims");
    s.Index = 1;
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Claims, 2UL | (1UL << 2) | (1UL << 4), "an unread bus's claims");
    /* The ladder's 16-entry limit: 64 KB of I/O steps 16 times. */
    snap_up(&s, 3);
    par_io(s.Par, 0x0000UL, 0xFFFFUL);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Steps[CM_BREQ_K_IO], 16, "64 KB of I/O: 16 steps");
    CHECK_EQ(p.Len[CM_BREQ_K_IO][15], 4 * KB, "the last 4 KB");
    /* The largest memory window: CAP, halved to 2 MB and then 1 MB. */
    snap_up(&s, 3);
    par_mem(s.Par, 0x00000000UL, 0xFFFFFFFFUL);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK_EQ(p.Len[CM_BREQ_K_MEM][0], CM_BREQ_CAP, "4 GB asked as CAP");
    CHECK(p.Steps[CM_BREQ_K_MEM] <= 16 &&
              p.Len[CM_BREQ_K_MEM][p.Steps[CM_BREQ_K_MEM] - 1] == MB,
          "the CAP ladder ends at 1 MB");
    /* A parent of 1 MB: the upstream ladder is that one entry. */
    snap_up(&s, 3);
    par_mem(s.Par, 0xE0000000UL, 0xE00FFFFFUL);
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CHECK(p.Steps[CM_BREQ_K_MEM] == 1 && p.Len[CM_BREQ_K_MEM][0] == MB,
          "a 1 MB parent: 1 MB");
}

/* ---- section 6's arithmetic ------------------------------------------- */

static void test_windows(void)
{
    ULONG par[6];
    ULONG p[3];

    memset(par, 0, sizeof(par));
    par_tunnel(par);
    CHECK(CmBreqWindows(par, p), "the tunnel port reads");
    CHECK_EQ(p[0], 12 * KB, "12 KB of I/O");
    CHECK_EQ(p[1], 384 * MB, "384 MB of memory");
    CHECK_EQ(p[2], 2 * MB, "2 MB prefetchable");
    par_pref64(par, 0, 0, 0, 0xFFEFFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == CM_BREQ_CAP, "exactly CAP");
    par_pref64(par, 0, 0, 0, 0xFFDFFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == CM_BREQ_CAP - MB,
          "1 MB under CAP");
    par_pref64(par, 0, 0, 0, 0xFFFFFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == CM_BREQ_CAP, "4 GB: capped");
    par_pref64(par, 0, 0, 1, 0x000FFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == CM_BREQ_CAP,
          "4 GB plus 1 MB: capped");
    par_pref64(par, 0, 0, 0xF, 0xFFFFFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == CM_BREQ_CAP, "64 GB: capped");
    par_pref64(par, 0, 0xFFF00000UL, 1, 0x000FFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == 2 * MB, "a window crossing 4 GB");
    par_pref64(par, 1, 0, 0, 0xFFFFFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == 0,
          "a base above the limit only in the high half: closed");
    par_pref64(par, 2, 0x00000000UL, 2, 0x001FFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[2] == 2 * MB,
          "a 2 MB window above 4 GB");
    par_mem(par, 0xFFF00000UL, 0xFFFFFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[1] == MB,
          "1 MB at the top of the 32-bit space");
    par_io(par, 0xF000UL, 0xFFFFUL);
    CHECK(CmBreqWindows(par, p) && p[0] == 4 * KB, "the last 4 KB of I/O");
    par[0] = 0xF1F1UL;
    par[5] = 0x00010001UL;
    CHECK(!CmBreqWindows(par, p), "an I/O window above 10000h: unread");
    par[0] = 0xF1F1UL;
    par[5] = 0x00010000UL;
    CHECK(!CmBreqWindows(par, p), "an I/O window reaching 10000h: unread");
    par[0] = 0x0101UL;
    par[5] = 0x00000001UL;
    CHECK(CmBreqWindows(par, p) && p[0] == 0,
          "a 32-bit I/O window closed in the upper half");
    par_closed(par, 0);
    par_closed(par, 1);
    par_closed(par, 2);
    CHECK(CmBreqWindows(par, p) && p[0] == 0 && p[1] == 0 && p[2] == 0,
          "closed windows read 0");
}

static void test_bars(void)
{
    ULONG bars[6];
    ULONG io;
    ULONG pref;

    memset(bars, 0, sizeof(bars));
    CHECK(CmBreqBars(bars, &io, &pref) && !io && !pref,
          "BARs reading 0 (the e1000e's before sizing): no I/O, no "
          "prefetchable claim from them");
    bars[0] = 0x4;
    bars[1] = 0x1;
    CHECK(CmBreqBars(bars, &io, &pref) && !io && !pref,
          "a 64-bit BAR's upper dword with bit 0 set is no I/O BAR");
    bars[1] = 0x8;
    CHECK(CmBreqBars(bars, &io, &pref) && !io && !pref,
          "a 64-bit BAR's upper dword with bit 3 set is no prefetchable one");
    bars[0] = 0xC;
    CHECK(CmBreqBars(bars, &io, &pref) && pref, "a prefetchable 64-bit BAR");
    memset(bars, 0, sizeof(bars));
    bars[5] = 0x4;
    CHECK(!CmBreqBars(bars, &io, &pref), "a 64-bit BAR in the last slot");
    bars[5] = 0x6;
    CHECK(!CmBreqBars(bars, &io, &pref), "a reserved width 11b");
    bars[5] = 0x1;
    CHECK(CmBreqBars(bars, &io, &pref) && io, "an I/O BAR in the last slot");
    memset(bars, 0, sizeof(bars));
    bars[2] = 0x1;
    CHECK(CmBreqBars(bars, &io, &pref) && io && !pref,
          "the e1000e: memory BARs and an I/O BAR");
}

/* ---- the diagnostic --------------------------------------------------- */

static void test_diag(void)
{
    ULONG d[CM_BQDIAG_WORDS];
    ULONG e[7];
    ULONG i;

    memset(d, 0, sizeof(d));
    memset(e, 0, sizeof(e));
    for (i = 1; i <= 10; i++) {
        e[0] = i;
        e[1] = (i == 3 ? CM_BREQ_OUT_NOMEM : CM_BREQ_OUT_REWRITTEN) << 12;
        CmBreqDiagAdd(d, i, e);
    }
    CHECK_EQ(d[0], 10, "the last sequence");
    CHECK_EQ(d[4], 3, "the oldest kept entry is the third");
    CHECK_EQ(d[4 + 7 * 8], 10, "the newest is last");
    CHECK_EQ(d[1] & 0xFFFF, 9, "nine rewrites");
    CHECK_EQ(d[3] & 0xFFFF, 1, "one allocation failure");
    CHECK(CmBreqDiagStart(d, 10, TRUE), "a start after a rewrite");
    CHECK(!CmBreqDiagStart(d, 10, TRUE), "seen once");
    CHECK(CmBreqDiagStart(d, 9, FALSE), "a failed start");
    CHECK_EQ(d[2], 1UL | (1UL << 16), "one start each way");
    CHECK_EQ((d[4 + 7 * 8 + 2] >> 22) & 3, CM_BREQ_START_OK,
             "the entry's start");
    CHECK(!CmBreqDiagStart(d, 3, TRUE), "no start for an entry not rewritten");
    CHECK(!CmBreqDiagStart(d, 0x77, TRUE), "no entry for that bridge");
    CmBreqDiagNoBridge(d);
    CHECK_EQ(d[3] >> 16, 1, "a child with no member above");
    d[1] = 0xFFFFUL;
    e[1] = CM_BREQ_OUT_REWRITTEN << 12;
    CmBreqDiagAdd(d, 11, e);
    CHECK_EQ(d[1], 0xFFFF, "the rewrites saturate");
    e[1] = CM_BREQ_OUT_SHAPE << 12;
    CmBreqDiagAdd(d, 12, e);
    CHECK_EQ(d[1] >> 16, 1, "a list left by rule");
}

/* ---- the invariants over a seeded sweep ------------------------------- */

static ULONG seed = 0x2026A045UL;

static ULONG rnd(ULONG n)
{
    seed = seed * 1103515245UL + 12345UL;
    return ((seed >> 8) & 0xFFFFFFUL) % n;
}

static void rnd_window(ULONG *par, ULONG k)
{
    ULONG units;
    ULONG base;

    if (rnd(5) == 0) {
        par_closed(par, k);
        return;
    }
    if (k == CM_BREQ_K_IO) {
        units = 1 + rnd(16);
        base = rnd(17 - units) * 0x1000UL;
        par_io(par, base, base + units * 0x1000UL - 1UL);
    } else if (k == CM_BREQ_K_MEM) {
        units = 1 + rnd(rnd(4) == 0 ? 4096 : 600);
        base = rnd(4097 - units) * MB;
        par_mem(par, base, base + units * MB - 1UL);
    } else if (rnd(4) == 0) {
        par_pref64(par, 0, 0, rnd(16), 0xFFFFFFFFUL);
    } else {
        units = 1 + rnd(64);
        par_pref(par, 0xC0000000UL, 0xC0000000UL + units * MB - 1UL);
    }
}

/* Every length the rules wrote a multiple of its granule, at least it, at
 * most the parent and CAP; every private descriptor of the input's, in its order; one
 * list; ListSize exact. */
static void check_list(const UCHAR *in, const UCHAR *out, ULONG bytes,
                       const CM_BREQ_PLAN *p, const char *what)
{
    ULONG count;
    ULONG i;
    ULONG j;
    ULONG len;
    ULONG k;
    const UCHAR *d;
    ULONG inCount;
    ULONG bad;

    bad = 0;
    count = get32(out + 36);
    if (get32(out) != bytes || bytes != 40 + 32 * count ||
        get32(out + 28) != 1) {
        bad++;
    }
    inCount = get32(in + 36);
    j = 0;
    for (i = 0; i < count; i++) {
        d = out + 40 + 32 * i;
        if (d[1] == 0x81) {
            while (j < inCount && memcmp(in + 40 + 32 * j, d, 32) != 0) {
                j++;
            }
            if (j == inCount) {
                bad++;
            }
            j++;
            continue;
        }
        len = get32(d + 8);
        k = d[1] == 1 ? 0UL : ((d[4] & 4) ? 2UL : 1UL);
        if (!p->Sized[k]) {
            /* A group kept as the OS list asked (rules 6.2.2, 6.3.2). */
            continue;
        }
        if (len == 0 || (len & ((k == 0 ? 0x1000UL : MB) - 1UL)) != 0 ||
            len > p->P[k] || len > CM_BREQ_CAP) {
            bad++;
        }
    }
    if (bad) {
        printf("  %s: %lu bad\n", what, bad);
    }
    CHECK_EQ(bad, 0, what);
}

static void test_sweep(void)
{
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;
    UCHAR out[2048];
    ULONG round;
    ULONG n;
    ULONG i;
    ULONG k;
    ULONG sum[3];
    ULONG w[3];
    ULONG got;
    ULONG minor;
    const UCHAR *list;
    ULONG sumBad;
    ULONG roomBad;
    ULONG lists;
    ULONG j;

    sumBad = 0;
    roomBad = 0;
    lists = 0;
    for (round = 0; round < 3000; round++) {
        minor = rnd(2);
        list = minor ? bqXpClosed : bq2kClosed;
        n = 2 + rnd(7);
        snap_down(&s, 0);
        for (k = 0; k < 3; k++) {
            rnd_window(s.Par, k);
        }
        for (i = 0; i < n; i++) {
            sib(&s, rnd(4), rnd(2), rnd(2), rnd(2));
        }
        (VOID)CmBreqWindows(s.Par, w);
        sum[0] = 0;
        sum[1] = 0;
        sum[2] = 0;
        for (i = 0; i < n; i++) {
            s.Index = i;
            if (CmBreqPlan(list, minor, &s, &p) != CM_BREQ_OUT_REWRITTEN) {
                continue;
            }
            memset(out, 0x5A, sizeof(out));
            got = CmBreqWrite(list, &p, out, sizeof(out));
            if (got == 0 || out[got] != 0x5A ||
                CmBreqWrite(list, &p, out, got - 1) != 0) {
                roomBad++;
                continue;
            }
            lists++;
            check_list(list, out, got, &p, "sweep: a downstream list");
            /* The preferred lengths as written: each group's first
             * descriptor (Option 0) that the rules sized. */
            for (j = 0; j < get32(out + 36); j++) {
                const UCHAR *d = out + 40 + 32 * j;
                ULONG kk;
                ULONG len;

                if (d[1] == 0x81 || d[0] != 0) {
                    continue;
                }
                kk = d[1] == 1 ? 0UL : ((d[4] & 4) ? 2UL : 1UL);
                if (!p.Sized[kk]) {
                    continue;
                }
                len = get32(d + 8);
                if (len > CM_BREQ_CAP - sum[kk]) {
                    sumBad++;
                } else {
                    sum[kk] += len;
                }
            }
        }
        for (k = 0; k < 3; k++) {
            if (sum[k] > (w[k] > CM_BREQ_CAP ? CM_BREQ_CAP : w[k])) {
                sumBad++;
            }
        }
        snap_up(&s, rnd(4) == 0 ? CM_BREQ_N_UNKNOWN : rnd(9));
        for (k = 0; k < 3; k++) {
            rnd_window(s.Par, k);
        }
        if (CmBreqPlan(list, minor, &s, &p) == CM_BREQ_OUT_REWRITTEN) {
            memset(out, 0x5A, sizeof(out));
            got = CmBreqWrite(list, &p, out, sizeof(out));
            if (got == 0 || out[got] != 0x5A) {
                roomBad++;
            } else {
                lists++;
                check_list(list, out, got, &p, "sweep: an upstream list");
            }
        }
    }
    CHECK_EQ(sumBad, 0, "sweep: the siblings' preferred lengths within W");
    CHECK_EQ(roomBad, 0, "sweep: the writer keeps to its room");
    CHECK(lists > 1000, "sweep: enough lists written");
}

/* ---- the bridge's interrupt after the template (task 45.55) ----------- */

/* The interrupt descriptor XP gave after its closed list on a switch port
 * whose Interrupt Pin is set (task 45.55's XP reading, LogConf of the
 * parent's 104C:8232 and 104C:8233): Option 0, CmResourceTypeInterrupt,
 * shared, level, vectors 0 to FFh. */
static const UCHAR bqIrqDesc[32] = {
    0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* `list` of `n` bytes with the interrupt descriptor appended, into out. */
static ULONG with_irq(const UCHAR *list, ULONG n, UCHAR *out)
{
    memcpy(out, list, n);
    memcpy(out + n, bqIrqDesc, 32);
    put32(out, n + 32UL);
    put32(out + 36, (ULONG)list[36] + 1UL);
    return n + 32UL;
}

static void test_irq_tail(void)
{
    UCHAR xp[sizeof(bqXpClosed) + 64];
    UCHAR w2k[sizeof(bq2kClosed) + 64];
    UCHAR m[sizeof(bqXpClosed) + 64];
    UCHAR a[2048];
    UCHAR b[2048];
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;
    CM_BREQ_PLAN q;
    ULONG nx;
    ULONG n2;
    ULONG i;
    ULONG bad;
    ULONG e[7];

    nx = with_irq(bqXpClosed, sizeof(bqXpClosed), xp);
    n2 = with_irq(bq2kClosed, sizeof(bq2kClosed), w2k);
    CHECK_EQ(nx, 0x288UL, "XP's list with the interrupt: 288h, as read");
    CHECK_EQ(CmBreqShape(xp, 1), CM_BREQ_SHAPE_XP,
             "XP's closed list and the bridge's interrupt: XP's template");
    CHECK_EQ(CmBreqShape(w2k, 0), CM_BREQ_SHAPE_2000,
             "2000's closed list and the bridge's interrupt: 2000's");
    CHECK_EQ(CmBreqShape(xp, 0), CM_BREQ_SHAPE_NONE,
             "XP's with the interrupt on 2000: left");
    CHECK_EQ(CmBreqShape(xp, 2), CM_BREQ_SHAPE_XP,
             "XP x64: XP's template, the interrupt after");
    CHECK_EQ(CmBreqShape(w2k, 2), CM_BREQ_SHAPE_NONE,
             "2000's with the interrupt on XP x64: left");
    CHECK_EQ(CmBreqTail(xp), 1, "the tail seen (XP)");
    CHECK_EQ(CmBreqTail(w2k), 1, "the tail seen (2000)");
    CHECK_EQ(CmBreqTail(bqXpClosed), 0, "no tail (XP)");
    CHECK_EQ(CmBreqTail(bq2kClosed), 0, "no tail (2000)");
    CHECK_EQ(CmBreqTail(NULL), 0, "no list, no tail");

    /* Every byte of the interrupt descriptor compared. */
    bad = 0;
    for (i = 0; i < 32; i++) {
        memcpy(m, xp, nx);
        m[sizeof(bqXpClosed) + i] ^= 0x01;
        if (CmBreqShape(m, 1) != CM_BREQ_SHAPE_NONE) {
            bad++;
            printf("  interrupt byte %lu accepted when changed\n", i);
        }
    }
    CHECK_EQ(bad, 0, "every field of the interrupt compared");
    /* The size and the count must agree with one trailing descriptor. */
    memcpy(m, xp, nx);
    put32(m, sizeof(bqXpClosed));
    CHECK_EQ(CmBreqShape(m, 1), CM_BREQ_SHAPE_NONE,
             "the interrupt counted but ListSize the template's: left");
    memcpy(m, xp, nx);
    put32(m + 36, 18);
    CHECK_EQ(CmBreqShape(m, 1), CM_BREQ_SHAPE_NONE,
             "ListSize with the interrupt but Count the template's: left");
    memcpy(m, xp, nx);
    memcpy(m + nx, bqIrqDesc, 32);
    put32(m, nx + 32UL);
    put32(m + 36, 20);
    CHECK_EQ(CmBreqShape(m, 1), CM_BREQ_SHAPE_NONE,
             "two interrupts: left");
    /* The interrupt in the I/O descriptor's place, the I/O last. */
    memcpy(m, xp, nx);
    memcpy(m + 40, bqIrqDesc, 32);
    memcpy(m + sizeof(bqXpClosed), bqXpClosed + 40, 32);
    CHECK_EQ(CmBreqShape(m, 1), CM_BREQ_SHAPE_NONE,
             "the interrupt first: left");
    CHECK_EQ(CmBreqShape(bq2kIrq, 0), CM_BREQ_SHAPE_NONE,
             "2000's configured list with an interrupt is still left");

    /* A rewrite keeps the interrupt last, unchanged: the list is the one
     * written without it, ListSize and Count one descriptor more. */
    dock3(&s, 1);
    CHECK_EQ(CmBreqPlan(bqXpClosed, 1, &s, &q), CM_BREQ_OUT_REWRITTEN,
             "ex1's NIC port, no interrupt: rewritten");
    CHECK_EQ(CmBreqPlan(xp, 1, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "ex1's NIC port, the interrupt after: rewritten");
    CHECK_EQ(p.Tail, 1, "the plan keeps the tail");
    CHECK_EQ(q.Tail, 0, "no tail planned without one");
    CHECK_EQ(p.Bytes, q.Bytes + 32UL, "one descriptor more");
    memset(a, 0x5A, sizeof(a));
    memset(b, 0x5A, sizeof(b));
    CHECK_EQ(CmBreqWrite(bqXpClosed, &q, a, q.Bytes), q.Bytes,
             "written without the interrupt");
    CHECK_EQ(CmBreqWrite(xp, &p, b, p.Bytes), p.Bytes,
             "written with the interrupt");
    CHECK_EQ(get32(b), p.Bytes, "ListSize");
    CHECK_EQ(get32(b + 36), get32(a + 36) + 1UL, "Count one more");
    CHECK(memcmp(a + 4, b + 4, 32) == 0 &&
              memcmp(a + 40, b + 40, q.Bytes - 40UL) == 0,
          "the windows as written without the interrupt");
    CHECK(memcmp(b + q.Bytes, bqIrqDesc, 32) == 0,
          "the interrupt last, as it came");
    CHECK(b[p.Bytes] == 0x5A, "nothing past the list");
    CHECK_EQ(CmBreqWrite(bqXpClosed, &p, b, sizeof(b)), 0,
             "a plan with the tail refuses a list without it");
    CHECK_EQ(CmBreqWrite(xp, &q, b, sizeof(b)), 0,
             "a plan without the tail refuses a list with it");
    /* 2000, the same. */
    CHECK_EQ(CmBreqPlan(bq2kClosed, 0, &s, &q), CM_BREQ_OUT_REWRITTEN,
             "2000: no interrupt, rewritten");
    CHECK_EQ(CmBreqPlan(w2k, 0, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "2000: the interrupt after, rewritten");
    CHECK_EQ(CmBreqWrite(bq2kClosed, &q, a, q.Bytes), q.Bytes,
             "2000 written without");
    CHECK_EQ(CmBreqWrite(w2k, &p, b, p.Bytes), p.Bytes, "2000 written with");
    CHECK(memcmp(a + 40, b + 40, q.Bytes - 40UL) == 0 &&
              memcmp(b + q.Bytes, bqIrqDesc, 32) == 0,
          "2000: the windows the same, the interrupt last");
    /* An upstream port that is left keeps the list as it came. */
    snap_up(&s, 1);
    CHECK_EQ(CmBreqPlan(xp, 1, &s, &p), CM_BREQ_OUT_LEFT,
             "a single enclosure's upstream port with an interrupt: left");
    /* The diagnostic marks the tail (dword 6 bit 24). */
    dock3(&s, 1);
    (VOID)CmBreqPlan(xp, 1, &s, &p);
    CmBreqEntry(&p, 0x0408UL, 0x0218UL, 1, p.Outcome, 1, e);
    CHECK_EQ(e[1], 2UL | (1UL << 4) | (1UL << 8) | (1UL << 12) |
                       (4UL << 16) | (1UL << 24),
             "entry: as ex1's NIC port, the tail kept");
    (VOID)CmBreqPlan(bqXpClosed, 1, &s, &p);
    CmBreqEntry(&p, 0x0408UL, 0x0218UL, 1, p.Outcome, 1, e);
    CHECK_EQ(e[1] >> 24, 0, "entry: no tail, bit 24 clear");
    /* Refused lists do not claim a tail. */
    CHECK_EQ(CmBreqPlan(bq2kIrq, 0, &s, &p), CM_BREQ_OUT_SHAPE,
             "the configured list: refused");
    CHECK_EQ(p.Tail, 0, "a refused list has no tail");
}

/* XP x64 (NT minor version 2): task 45.55 read its closed lists of the
 * hot-added switch's ports byte for byte equal to XP's, the bridge's
 * interrupt last (record 33 section 7.7, revision 5), so it takes XP's
 * template and is planned and written exactly as XP. */
static void test_xp64(void)
{
    UCHAR xp[sizeof(bqXpClosed) + 64];
    UCHAR a[2048];
    UCHAR b[2048];
    CM_BREQ_SNAP s;
    CM_BREQ_PLAN p;
    CM_BREQ_PLAN q;
    ULONG nx;
    ULONG e[7];
    ULONG f[7];

    nx = with_irq(bqXpClosed, sizeof(bqXpClosed), xp);
    CHECK_EQ(nx, 0x288UL, "XP x64's list as read: 288h");
    CHECK_EQ(CmBreqShape(xp, 2), CM_BREQ_SHAPE_XP,
             "XP x64's list as read: XP's template");
    CHECK_EQ(CmBreqShape(bqXpConfigured, 2), CM_BREQ_SHAPE_NONE,
             "XP's configured list on XP x64: left");
    CHECK_EQ(CmBreqShape(bqXpClosed, 3), CM_BREQ_SHAPE_NONE,
             "an NT 5.x minor version no template names: left");
    CHECK_EQ(CmBreqShape(xp, 3), CM_BREQ_SHAPE_NONE,
             "the same with the interrupt: left");

    /* Planned and written as on XP, the tail kept. */
    dock3(&s, 1);
    CHECK_EQ(CmBreqPlan(xp, 1, &s, &q), CM_BREQ_OUT_REWRITTEN,
             "XP: ex1's NIC port with the interrupt, rewritten");
    CHECK_EQ(CmBreqPlan(xp, 2, &s, &p), CM_BREQ_OUT_REWRITTEN,
             "XP x64: the same port, rewritten");
    CHECK_EQ(p.Bytes, q.Bytes, "the same size as on XP");
    CHECK_EQ(p.Tail, 1, "XP x64: the tail kept");
    memset(a, 0x5A, sizeof(a));
    memset(b, 0x5A, sizeof(b));
    CHECK_EQ(CmBreqWrite(xp, &q, a, q.Bytes), q.Bytes, "XP written");
    CHECK_EQ(CmBreqWrite(xp, &p, b, p.Bytes), p.Bytes, "XP x64 written");
    CHECK(memcmp(a, b, p.Bytes) == 0, "XP x64 writes XP's list");
    CHECK(b[p.Bytes] == 0x5A, "nothing past the list");
    /* The diagnostic: XP's shape, the system 2. */
    CmBreqEntry(&q, 0x0408UL, 0x0218UL, 1, q.Outcome, 1, e);
    CmBreqEntry(&p, 0x0408UL, 0x0218UL, 2, p.Outcome, 1, f);
    CHECK_EQ((f[1] >> 4) & 0xFUL, CM_BREQ_SHAPE_XP, "entry: XP's shape");
    CHECK_EQ((f[1] >> 8) & 0xFUL, 2, "entry: the system 2");
    CHECK_EQ(f[1] & ~0xF00UL, e[1] & ~0xF00UL,
             "entry: as XP's but the system");
    /* A single enclosure's upstream port stays as it came, as on XP. */
    snap_up(&s, 1);
    CHECK_EQ(CmBreqPlan(xp, 2, &s, &p), CM_BREQ_OUT_LEFT,
             "XP x64: a single port's upstream port, left as on XP");
}

int main(void)
{
    test_templates();
    test_example1();
    test_irq_tail();
    test_xp64();
    test_example2();
    test_example3();
    test_example4();
    test_example5();
    test_example6();
    test_edges();
    test_windows();
    test_bars();
    test_diag();
    test_sweep();
    printf("test_cm_breq: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
