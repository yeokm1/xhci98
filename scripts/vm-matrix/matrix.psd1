<#
    The Phase 10 device matrix: one row per (QEMU device model, speed variant),
    with what each row is expected to move.

    READ docs\contributing\design\06-device-matrix-verdict.md FIRST.  The expectation
    language, the five outcomes, and why `zero` and `inert` are not
    interchangeable are all there and are not repeated here.

    The device population is task 10.1's, measured from the installed QEMU into
    out\phase10\device-population.txt - not recalled.  Every model below is one
    that probe-devices.ps1 attached to a qemu-xhci and read back.

    WHY MOST ROWS LOOK THE SAME.  The expectations state what THIS DRIVER must
    do, because that is what is knowable in advance.  Whether a function driver
    binds is a guest fact this project has never measured for most of these
    models, and the design does not ask the matrix to predict it: a row that is
    addressed and never claimed resolves to NODRIVER by derivation, from
    `devices addressed` moving while `endpoints opened` does not.  Writing a
    guess into the file and calling the guess an expectation would make the
    first run confirm the guess rather than measure the fact.

    WHY NO ROW ASSERTS `advance transfers submitted`, WHICH IS THE OBVIOUS
    THING TO ASSERT.  Windows 98 idle-suspends the controller about half a
    second after the last transfer, and an attach onto a halted controller is
    invisible to the whole stack - so a keep-alive USB pointer is present and
    `mouse_move` is pumped through it immediately before each attach.  Those
    pumped moves are transfers, they are on the SAME controller, and every
    transfer counter in the extension is controller-wide.  So `transfers
    submitted` advances on every row in a pumped group whether the row's own
    device moved a byte or not, and asserting it would be a check that passes by
    construction - the failure mode this whole design exists to avoid.

    What the pump CANNOT move is the per-attach counters: `devices addressed`,
    `slots enabled` and `endpoints opened` fire on an enumeration, not on
    traffic, and the keep-alive is enumerated once at group start rather than
    once per row.  Those carry the claim instead.  The IDENTITIES are unaffected
    either way - they hold over whatever traffic occurred, no matter who caused
    it - which is a second reason to prefer them over counters.

    Device-class counters (`iso packets answered`, the topology block) are also
    safe, because the keep-alive is a HID and generates none of them.

    `ExpectNoDriver = @{ '<target>' = '<reason>' }` IS READ ONLY BY THE
    POST-RELEASE RUN (run-matrix.ps1 -PostRelease; design record 09 section
    4.2).  It says a freshly installed copy of that operating system is known
    to carry no class driver for the model, so a NODRIVER there does not count
    against the target's verdict.  The outcome word does not change and the
    row is still printed.  Every entry below is a guess from Phase 10's
    measurements on the carried-along images, written before the first fresh
    run, and the first fresh run is where they get corrected: a row that
    reaches NODRIVER without an entry counts against the target, and a row
    with an entry that reaches anything else is printed as a stale entry.  A
    fresh target inherits the entries of the target it names in `Like`.
#>
@{
    Schema = 1

    # Applied to every row in every group.  These are the assertions that hold
    # regardless of what the device is or whether anything above usbport wanted
    # it, so a device the OS ignores still checks them.
    Always = @(
        # Ours: the port change was answered and the device was addressed.  A
        # row where this does not move is a failure of this driver, and it is
        # what separates FAIL from NODRIVER.
        'advance devices addressed'
        'advance slots enabled'

        # Failure-shaped.  Each of these is a path that EXISTS on every row and
        # must not be taken - which is what makes them `zero` and not `inert`.
        'zero fatal controller status'
        'zero transfer events for no open endpoint'
        'zero endpoint opens refused - unusable buffer'
        'zero endpoint opens refused - malformed call'
        'zero interrupt mask failures'
        'zero commands the engine gave up on'

        # The nine-term open-accounting identity, transcribed from src\xhci.h's
        # own statement of it.  `EP0 opens refused - no route` is a SHARE of
        # `EP0 opens refused` and is deliberately absent.
        'identity endpoint opens seen == endpoint opens accepted + EP0 opens refused + endpoint opens refused - unusable buffer + endpoint opens refused - malformed call + endpoint refusals - type + endpoint refusals - no device + endpoint refusals - not ready + endpoint refusals - params + endpoint refusals - ring pool'
    )

    Groups = @(

        # -------------------------------------------------------------------
        @{
            Name = 'hid'
            Description = 'Human interface devices on a root port, at both speeds.'
            # A USB pointer is present for the whole group and mouse_move is
            # pumped through it: Windows 98 idle-suspends the controller about
            # half a second after the last transfer, and an attach onto a halted
            # controller is invisible to the whole stack (batch 7a-V).
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-kbd/hs'
                    Model = 'usb-kbd'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    Expect = @(
                        'advance endpoints opened >= 1'
                        # HS device, so Phase 5 task 7's untruth costs nothing
                        # here - the root port really is reported High Speed and
                        # the device really is one.
                        'zero endpoint speed mismatches'
                    )
                }
                @{
                    Name = 'usb-kbd/fs'
                    Model = 'usb-kbd'
                    AddArgs = 'usb_version=1'
                    Settle = 20
                    Expect = @(
                        'advance endpoints opened >= 1'
                        # EXPECTED NONZERO, and this is the point of the row.
                        # Phase 5 task 7 makes this driver report every
                        # connected root port as High Speed, so a Full Speed
                        # device on one is a mismatch BY CONSTRUCTION.  A zero
                        # here would mean that untruth had been removed, not
                        # that nothing happened - which is why it is an
                        # `advance` and not a `zero`.
                        'advance endpoint speed mismatches'
                    )
                }
                @{
                    Name = 'usb-mouse/hs'
                    Model = 'usb-mouse'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    Expect = @(
                        'advance endpoints opened >= 1'
                    )
                    # Phase 10's whole-matrix 2b run read NODRIVER for both
                    # mouse rows while the keyboards and the tablet bound: a
                    # second pointer beside the keep-alive mouse is what that
                    # guest did not claim.  Written as measured; the first
                    # fresh run says whether an SP4 with no history does the
                    # same.
                    ExpectNoDriver = @{
                        '2b' = 'measured NODRIVER on the carried-along 2b image (a second usb-mouse beside the keep-alive); a fresh SP4 has no class driver that image lacked'
                    }
                }
                @{
                    Name = 'usb-mouse/fs'
                    Model = 'usb-mouse'
                    AddArgs = 'usb_version=1'
                    Settle = 20
                    Expect = @(
                        'advance endpoints opened >= 1'
                        'advance endpoint speed mismatches'
                    )
                    ExpectNoDriver = @{
                        '2b' = 'measured NODRIVER on the carried-along 2b image (a second usb-mouse beside the keep-alive); a fresh SP4 has no class driver that image lacked'
                    }
                }
                @{
                    Name = 'usb-tablet/hs'
                    Model = 'usb-tablet'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    Expect = @(
                        'advance endpoints opened >= 1'
                    )
                    # EXCLUDED ON WINDOWS 98, and the row stays here so that the
                    # limitation is printed in every report rather than being
                    # deleted into silence - the same reason NODRIVER and INERT
                    # exist.  Three of the four QEMU main-loop hangs
                    # happened while Windows 98 was installing the driver for
                    # `usb-tablet`: CPU time frozen, `Responding = False`, monitor
                    # and display dead together, image intact afterwards.  It is
                    # an ABSOLUTE pointing device, so binding it makes QEMU switch
                    # its active pointer and its display backend to absolute mode
                    # on the main loop - a candidate mechanism, NOT a demonstrated
                    # one.  Nothing here has isolated it, and three earlier
                    # explanations were each refuted by the next boot.
                    #
                    # This is a VEHICLE limitation, not a driver result: with the
                    # DUT port pinned the row reaches NODRIVER, i.e. this driver
                    # enumerates the tablet correctly and only the OS bind - the
                    # step that hangs QEMU - is missing.
                    ExcludedOnTarget = @{
                        '2a' = 'installing this driver hangs QEMU on Windows 98 - 3 of 4 main-loop hangs measured; unresolved, and it is the vehicle rather than the miniport (the row reaches NODRIVER, so enumeration works)'
                    }
                }
                @{
                    Name = 'usb-wacom-tablet/fs'
                    Model = 'usb-wacom-tablet'
                    Settle = 20
                    Expect = @(
                        'advance endpoints opened >= 1'
                    )
                    ExcludedOnTarget = @{
                        '2a' = 'excluded with usb-tablet/hs - the same absolute-pointer install path, never taught to the image for that reason'
                    }
                    ExpectNoDriver = @{
                        '2b' = 'measured NODRIVER on the carried-along 2b image; Windows 2000 SP4 ships no driver for a Wacom tablet'
                    }
                }
            )
        }

        # -------------------------------------------------------------------
        @{
            Name = 'storage'
            Description = 'Mass storage, the class that exercises bulk in both directions.'
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-storage/hs'
                    Model = 'usb-storage'
                    # `removable=on` is NOT cosmetic: without it Windows 98 gives
                    # the disk no drive letter while both devnodes read "working
                    # properly" and the SCSI exchange completes cleanly (task
                    # 8-V.1).  The flag lived only in an unrecorded monitor
                    # device_add for a whole session because of that.
                    AddArgs = 'drive=matrixdrv,removable=on'
                    NeedsDrive = $true
                    Settle = 30
                    Expect = @(
                        'advance endpoints opened >= 2'
                        # Opt-in, and being TESTED rather than assumed - see
                        # design doc 06 section 3.2 on why this identity is not
                        # in Always.
                        'identity transfers submitted == transfers completed + transfers cancelled'
                    )
                }
                # usb-bot AND usb-uas ARE SCSI HOST ADAPTERS, NOT DISKS.
                #
                # They have no `drive` property at all - `Property 'usb-bot.drive'
                # not found` - and QEMU attaches them happily with nothing behind
                # them, which is how they spent three runs failing `advance
                # endpoints opened` as though this driver had ignored a device on
                # the bus.  An empty adapter is a USB device with no LUN, so
                # Windows has nothing to bind and the row measures nothing.
                #
                # `Child` attaches a scsi-hd on the adapter's own SCSI bus after
                # the adapter itself is on the USB bus, which is the two-step
                # QEMU requires and the only way these rows exercise bulk.
                #
                # EACH OF THESE TWO ROWS HAS ITS OWN DRIVE, AND THAT IS NOT
                # TIDINESS.  Both used `matrixdrv2`, and QEMU auto-deletes a
                # `-drive` backend when its guest device is unplugged - so once
                # `usb-bot/fs` had run and been torn down, the id was GONE and
                # `usb-uas/fs` died on `Property 'scsi-hd.drive' can't find value
                # 'matrixdrv2'`, reported as that row's own refusal.  Two
                # whole-matrix runs recorded it that way.  The message is what
                # separates the two possible causes: a backend that exists but is
                # taken says `already in use`, so `can't find value` means it no
                # longer exists and no teardown ordering can help.
                @{
                    Name = 'usb-bot/fs'
                    Model = 'usb-bot'
                    Child = 'scsi-hd,bus={ID}.0,drive=matrixdrv2'
                    NeedsDrive2 = $true
                    Settle = 30
                    Expect = @(
                        'advance endpoints opened >= 1'
                    )
                }
                @{
                    Name = 'usb-uas/fs'
                    Model = 'usb-uas'
                    Child = 'scsi-hd,bus={ID}.0,drive=matrixdrv3,scsi-id=0,lun=0'
                    NeedsDrive2 = $true
                    Settle = 30
                    Expect = @(
                        'advance endpoints opened >= 1'
                    )
                    ExpectNoDriver = @{
                        '2a' = 'measured NODRIVER on both legs of the second post-release run (2026-08-30, fresh Windows 98 SE guest, class taught): the adapter is addressed and neither Windows 98 SE nor NUSB 3.3 has a UAS class driver'
                        '2b' = 'measured NODRIVER on both legs of the first post-release run (2026-08-30, fresh Windows 2000 SP4 guest): the adapter is addressed, Windows raises a Found New Hardware wizard and has no UAS class driver to offer; the wizard does not block enumeration on Windows 2000'
                    }
                }
            )
        }

        # -------------------------------------------------------------------
        @{
            Name = 'hub'
            Description = 'An external hub on a root port - the topology path.'
            # THE PUMP IS ON, AND THE REASON IT WAS OFF WAS CIRCULAR.
            #
            # Batch 7b-V measured that a hub tree stops Windows 98
            # idle-suspending, and this group was written with `Pump = $false` on
            # the strength of it.  But that is true only ONCE THE HUB IS
            # ATTACHED, and the attach is the thing that needs a live
            # controller: Windows 98 suspends about half a second after the last
            # transfer, and an attach onto a halted controller is invisible to
            # the whole stack.  Measured on the prepared 2a image -
            # `usb-hub/fs` failed with `devices addressed +0`, nothing enumerated
            # at all, on a target where the hub had just been installed by hand.
            #
            # A precondition that the step itself establishes cannot be assumed
            # before the step.
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-hub/fs'
                    Model = 'usb-hub'
                    Settle = 30
                    Expect = @(
                        'advance endpoints opened >= 1'
                        'advance topology: hub descriptors folded'
                        'advance topology: hub slots marked'
                        'zero topology: hub descriptors malformed'
                        'zero topology: nodes dropped'
                        'zero topology: behind-hub refused - no record'
                    )
                }

                # TASK 10.4's SECOND CLAUSE: reproduce a hand-measured result.
                #
                # Batch 7b-V drove a hub CHURN sequence by hand and recorded
                # numbers this harness must be able to produce - a move rebuilding
                # the route `0x00001` -> `0x00003`, a whole-hub removal taking
                # three live devices leaf-first with nothing stranded, and
                # `TtPairsDisagreed` = 10 as the phantom-TT control.  A single hub
                # attach cannot produce any of those, which is why this row has
                # `Steps` and the others do not.
                #
                # The expectations below are deliberately the SHAPE of the hand
                # run rather than its exact totals: the counts depend on how many
                # devices the sequence touches, and asserting 10 before measuring
                # would be predicting the answer rather than testing it.  The
                # first run records what this sequence produces; only then can it
                # be compared with the hand numbers, and a DISAGREEMENT is the
                # point - "a harness that has never disagreed with a hand-run is
                # untested, not correct".
                @{
                    Name = 'usb-hub/churn'
                    Model = 'usb-hub'
                    Settle = 25
                    Steps = @(
                        # A BEHIND-HUB DEVICE IS A PORT PATH ON THE SAME BUS, NOT
                        # A BUS OF ITS OWN.  Every step here used to say
                        # `bus={DUT}.0`, and QEMU answered `Bus 'dut2.0' not
                        # found` - a `usb-hub` creates no bus, and a child is
                        # addressed by a hierarchical `port=` under the hub's own
                        # root port.  So the whole churn sequence had NEVER
                        # EXECUTED on any target, which is the real reason batch
                        # 7b-V's `TtPairsDisagreed` = 10 was never reproduced:
                        # the row that was to reproduce it could not attach its
                        # first child.  It was invisible because the row is
                        # EXCLUDED on 2a and 2b's five groups had never run in
                        # one invocation, so the failure sat in a group nobody
                        # reached twice.  Confirmed guestless:
                        # `bus=h1.0` is refused, `port=2.1` and `port=2.2.1` both
                        # attach and `info usb` reports them as `Port 2.1` and
                        # `Port 2.2.1`.
                        #
                        # `{PORT}` is the root port the runner pinned the hub to
                        # (-DutPort), which is the only place that number is
                        # known.
                        #
                        # A child on the hub, then off, then back on the SAME
                        # port, then onto a DIFFERENT one - QEMU has no "move",
                        # and a delete plus an add elsewhere is what a physical
                        # move is on the wire anyway.
                        # THE SEQUENCE MIRRORS THE HAND RUN DEVICE FOR DEVICE,
                        # because the number it has to reproduce is a COUNT OF
                        # BEHIND-HUB ENUMERATIONS.  Batch 7b-V's own words:
                        # "every one of the TEN behind-hub enumerations had
                        # usbport name a translator the graph refused to
                        # believe".  A first version of this row presented five
                        # and duly read `TtPairsDisagreed` = 5 - not a
                        # contradiction of the hand run, just a different tree.
                        #
                        # Two other differences to `scripts\local\hub7bv.ps1`
                        # are closed here: it creates hubs with `ports=8`, and
                        # its children carry NO `usb_version=`, so they take
                        # QEMU's default - which a guestless probe measured as
                        # **12 Mb/s**, where this row had been forcing
                        # `usb_version=2`.  A speed the hand run did not use is
                        # a different measurement wearing the same name.
                        #
                        # Counting the behind-hub enumerations below: the row's
                        # own `Model` puts hub1 on the root port (not itself
                        # behind a hub), then 1-5 are the churn and 6-10 are the
                        # five-tier chain and the device under it.
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch1,bus=xhci.0,port={PORT}.1'; Wait = 20 }          # 1
                        @{ Do = 'del'; Id = 'ch1'; Wait = 12 }
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch1,bus=xhci.0,port={PORT}.1'; Wait = 20 }          # 2 - same port
                        @{ Do = 'del'; Id = 'ch1'; Wait = 12 }
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch1,bus=xhci.0,port={PORT}.3'; Wait = 20 }          # 3 - moved
                        # A second tier, so the teardown has to go leaf-first.
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch2,bus=xhci.0,port={PORT}.2,ports=8'; Wait = 20 }    # 4
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch3,bus=xhci.0,port={PORT}.2.1'; Wait = 20 }        # 5
                        # The five-tier chain, one hub per tier off port 1 of the
                        # one above - hub1 is this row's own device, so four more
                        # hubs reach the ceiling - and then a device at tier 5.
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch4,bus=xhci.0,port={PORT}.1,ports=8'; Wait = 15 }    # 6
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch5,bus=xhci.0,port={PORT}.1.1,ports=8'; Wait = 15 }  # 7
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch6,bus=xhci.0,port={PORT}.1.1.1,ports=8'; Wait = 15 }# 8
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch7,bus=xhci.0,port={PORT}.1.1.1.1,ports=8'; Wait = 15 } # 9
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch8,bus=xhci.0,port={PORT}.1.1.1.1.1'; Wait = 20 }  # 10
                    )
                    Expect = @(
                        # THE HAND-MEASURED NUMBER, ASSERTED RATHER THAN READ.
                        # Task 10.4 requires this harness to reproduce a result
                        # measured by hand, and batch 7b-V measured exactly ten
                        # disagreements on BOTH targets - one per behind-hub
                        # enumeration, against `TtProgrammed` 0 and
                        # `TtPairsAgreed` 0.  The sequence above presents ten, so
                        # anything else here is a real disagreement with the hand
                        # run and must fail the row rather than be read off.
                        'advance topology: TT pairs disagreeing with usbport >= 10'
                        'advance topology: hub descriptors folded >= 2'
                        'advance topology: behind-hub opens >= 2'
                        'advance topology: behind-hub devices addressed >= 2'
                        'zero topology: hub descriptors malformed'
                        'zero topology: nodes dropped'
                        'zero topology: behind-hub refused - no record'
                        'zero topology: behind-hub refused - too deep'
                    )
                    # Windows 98 would raise a modal wizard for each behind-hub
                    # device instance, none of which the image has been taught,
                    # so this row is a 2b clause until a prep pass covers them.
                    ExcludedOnTarget = @{
                        '2a' = 'behind-hub device instances are not taught to this image; each raises a modal wizard that blocks the bind - prep them at their hub ports first'
                    }
                }
            )
        }

        # -------------------------------------------------------------------
        @{
            Name = 'other'
            Description = 'Everything else the population holds. Most of these have never been presented to this driver on any target, and none of their bind outcomes are predicted here.'
            Pump = $true
            Rows = @(
                # The fresh-install NODRIVER entries on this group are the
                # classes neither target ships a driver for, each measured
                # NODRIVER on the carried-along image of the target named
                # (the 2a whole-matrix FAILs on these rows were the runner's
                # second-target backend defect, since fixed, not a class
                # fact).  Guesses until the first fresh run; see the header.
                @{
                    Name = 'usb-net/fs'
                    Model = 'usb-net'
                    AddArgs = 'netdev=matrixnet'
                    NeedsNetdev = $true
                    Settle = 30
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'measured NODRIVER on the carried-along 2a image; neither Windows 98 SE nor NUSB 3.3 ships a driver for a CDC/RNDIS Ethernet function'
                        '2b' = 'measured NODRIVER on the carried-along 2b image; Windows 2000 SP4 ships no RNDIS or CDC Ethernet class driver'
                    }
                }
                @{
                    Name = 'usb-serial/fs'
                    Model = 'usb-serial'
                    AddArgs = 'chardev=matrixchr1'
                    NeedsChardev = 1
                    Settle = 25
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'no class driver for a vendor-class serial adapter on Windows 98 SE or in NUSB 3.3'
                        '2b' = 'no class driver for a vendor-class serial adapter on Windows 2000 SP4'
                    }
                }
                @{
                    Name = 'usb-braille/fs'
                    Model = 'usb-braille'
                    AddArgs = 'chardev=matrixchr2'
                    NeedsChardev = 2
                    Settle = 25
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'no driver for a Baum braille display on Windows 98 SE or in NUSB 3.3'
                        '2b' = 'no driver for a Baum braille display on Windows 2000 SP4'
                    }
                }
                @{
                    Name = 'usb-ccid/fs'
                    Model = 'usb-ccid'
                    Settle = 25
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'measured NODRIVER on the carried-along 2a image; Windows 98 SE has no CCID class driver'
                        '2b' = 'measured NODRIVER on the carried-along 2b image; the CCID class driver arrived with Windows XP'
                    }
                }
                @{
                    Name = 'u2f-emulated/fs'
                    Model = 'u2f-emulated'
                    Settle = 25
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2b' = 'measured NODRIVER on the carried-along 2b image; a HID with no boot interface that Windows 2000 did not claim'
                    }
                }
            )
        }

        # -------------------------------------------------------------------
        @{
            Name = 'audio'
            Description = 'The isochronous path. Its own group because on Windows 98 this device bugchecks the guest, and a group boundary is the blast radius.'
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-audio/fs'
                    Model = 'usb-audio'
                    Settle = 35
                    Expect = @( 'advance endpoints opened >= 1' )
                    # Per-target additions.  These are the only predicted bind
                    # outcomes in the whole file, and both are predicted because
                    # they were MEASURED in batch 9-V rather than guessed.
                    ExpectByTarget = @{
                        # AN UNATTENDED RUN PLAYS NOTHING, AND THAT IS WHY THESE
                        # ARE INERT RATHER THAN ADVANCE.  A first version of this
                        # row asserted `advance iso submits` on 2b, on the
                        # strength of batch 9-V having measured 250,330 packets
                        # there - and the run duly reported FAIL.  The mistake is
                        # instructive: batch 9-V's isochronous traffic came from
                        # a 48 kHz tone being PLAYED into the device, by an
                        # operator, with a wav capture as the oracle.  Attaching
                        # a usb-audio device and waiting produces an idle audio
                        # endpoint and no isochronous traffic whatsoever.
                        #
                        # So the matrix does not claim the isochronous path here.
                        # Design doc 06 section 7 already says this harness does
                        # not judge audio, because the traced build's own
                        # per-line output is what stutters a stream on this
                        # vehicle - the `qemu` flavour since task 13-L.1, which
                        # is the one this harness runs; this
                        # is the same boundary met from the other side.  Phase 9's
                        # hand-run with a capture remains the oracle, and these
                        # counters are recorded as structurally zero HERE rather
                        # than quietly dropped, so the row cannot read as a pass.
                        '2b' = @(
                            'inert iso packets answered because nothing in an unattended run plays audio - batch 9-V needed a tone and a wav capture, and an idle usb-audio endpoint moves no isochronous traffic at all'
                            'zero iso missed service errors'
                            'zero iso packet errors'
                        )
                        # Windows 98 SE's own USBAUDIO.VXD divides by zero after
                        # exactly one 10 ms URB - reproduced four times in batch
                        # 9-V, twice through a UHCI control with this driver
                        # idle-suspended and every isochronous counter at 0.
                        # The path exists but the target destroys itself on it,
                        # so the row is inert here rather than expected to fail.
                        '2a' = @(
                            'inert iso packets answered because Windows 98 SE USBAUDIO.VXD faults after one URB - exonerated in batch 9-V through a UHCI control'
                        )
                    }
                    # This row is expected to be able to kill the 2a guest.  The
                    # runner treats that as the end of the GROUP, not the run.
                    MayWedgeGuest = @('2a')
                }
            )
        }
    )
}
