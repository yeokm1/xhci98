<#
    The device matrix's SECOND EXPECTATION SET: the successor host controller
    driver's (roadmap-hcd.md task 26-A.10; design record 13 section 9.6).

    READ docs\contributing\design\06-device-matrix-verdict.md FIRST, then
    matrix.psd1's header.  The expectation language, the five outcomes, why
    `zero` and `inert` are not interchangeable, why no row asserts a
    controller-wide traffic counter, the keep-alive pump and `ExpectNoDriver`
    are all there and hold here unchanged.  So do design record 06's verdict
    rules: lib\verdict.ps1's Get-RowOutcome runs in the same order for both
    sets, and only the counters it names differ.

    WHY A SECOND SET.  matrix.psd1 encodes the miniport's behaviour, not a
    device's.  Its `endpoint speed mismatches` rows assert usbport's
    High-Speed lie on a root port with the virtual-hub switch off, its switch
    rows assert the virtual hub's extra tier and usbport's naming of it, and
    its counters are read at the miniport extension's offsets - so a literal
    field-for-field comparison would fail a correct HCD.  matrix.psd1 is kept
    as it was, frozen, as the record of what 1.2.0.0 was judged by (its
    offsets.txt is that binary's; the tree no longer builds it).  This file is
    the one run-matrix.ps1 reads by default.

    THE DEVICE POPULATION IS matrix.psd1's.  Groups, row names, models,
    AddArgs, Child, Steps, Settle, NeedsNetdev / NeedsChardev, Pump,
    ExcludedOnTarget, ExpectNoDriver and MayWedgeGuest are copied unchanged:
    they are facts about QEMU and the guests, not about either driver, and
    their reasons are in matrix.psd1 beside each one.  Only the expectations
    differ.

    WHAT THE HARNESS ADDS FOR THIS SET (`Driver = 'hcd'`):

      - EVERY ROW CARRIES `ExpectedSpeed`, the speed QEMU presents the device
        at on an xHCI port (measured by probe-devices.ps1 into
        out\phase10\device-population.txt: 480 Mb/s for usb-kbd and usb-mouse
        at usb_version=2, usb-tablet and usb-storage; 12 Mb/s for everything
        else; no QEMU model is Low Speed).  The harness writes the lines
        itself (lib\verdict.ps1, Get-HcdSpeedExpectationTexts): the speed the
        HCD decoded from the port and the speed it programmed into the Slot
        Context each advance by exactly the row's device count at that speed
        and by zero at the other two, and `slot speed disagreeing with port
        speed` stays zero.  A wrong speed report is a FAIL, never a silence.
        This replaces the miniport's `endpoint speed mismatches`, which has no
        HCD counterpart and leaves the executable expectations altogether.
      - The validator refuses a row with no Expect or no ExpectedSpeed, and
        refuses ExpectBySwitch: every row is decided here, none inherited by
        omission, and the HCD has no virtual-hub switch.
      - Labels that name usbport or the virtual hub are refused by name, with
        the reason (lib\verdict.ps1, $script:HcdRetiredLabels).  They are not
        written as `inert`: the harness resolves an inert label to a real
        counter field and reads it, so an absent field would be an ERROR
        rather than a reading.  Where a row of matrix.psd1 carried one, the
        row below says what replaced it.
      - The counters are read from the HCD's counter block (XHCIHC_COUNTERS,
        design record 13 section 9.4) at offsets-hcd.txt's offsets, and the
        block is found by its `counters start= / size= / VA ...` lines on
        the debug console (lib\counters.ps1, Find-CounterBlockIdentity).

    THE HUB ROWS ARE PHASE 27'S.  Until the bus serves hubs itself (27-A.1 to
    27-A.3) it offers a hub as a device with no driver and enumerates nothing
    behind it (4e79718), so `usb-hub/fs` reads NODRIVER and `usb-hub/churn`
    reads FAIL on the HCD of Phase 26.  Both are written for the HCD that
    serves hubs, and Phase 26's runs take the root-port groups:
    `-Group audio,hid,storage,other`.
#>
@{
    Schema = 2
    Driver = 'hcd'

    Always = @(
        # The bus's own Enable Slot and Address Device: what separates FAIL
        # from NODRIVER, as in matrix.psd1.
        'advance devices addressed'
        'advance slots enabled'

        # Failure-shaped paths that exist on every row.  `transfer events for
        # no open endpoint` is the event DPC finding no pipe for a transfer
        # event; `commands the engine gave up on` is a command the bus's
        # thread abandoned on its watchdog.
        'zero fatal controller status'
        'zero transfer events for no open endpoint'
        'zero interrupt mask failures'
        'zero commands the engine gave up on'

        # Replaces matrix.psd1's `endpoint opens refused - unusable buffer`
        # and `- malformed call`, which were the buffer and call shape usbport
        # handed OpenEndpoint: the HCD parses the URBs itself, and this is it
        # refusing one whose header, length, buffer or MDL it cannot use.
        'zero URBs refused - malformed'

        # THIS DRIVER REFUSING A FUNCTION DRIVER'S REQUEST - the HCD's refusal
        # set (lib\verdict.ps1, $script:HcdRefusalLabelsPermanent), which
        # Get-RowOutcome reads whether or not a line here names it.  The first
        # three are the bus's SELECT_CONFIGURATION / SELECT_INTERFACE refusals
        # under the labels the miniport's open path used for the same three
        # causes; the last three are the Configure Endpoint completion classes.
        # matrix.psd1's `endpoint refusals - no device` (usbport's device
        # handle) and the transient `- not ready` (usbport's retry) have no
        # HCD counterpart and are retired.
        'zero endpoint refusals - type'
        'zero endpoint refusals - params'
        'zero endpoint refusals - ring pool'
        'zero endpoint configure failures'
        'zero endpoints refused - no bandwidth'
        'zero endpoints refused - no resources'
        # A select answered with a failure, whatever moved above: one whose
        # endpoints opened and whose SET_CONFIGURATION / SET_INTERFACE then
        # failed moves none of the six. Not the BUFFER_TOO_SMALL probe.
        'zero selects failed'

        # Replaces the miniport's nine-term open-accounting identity, which
        # was usbport's OpenEndpoint accounting.  Every non-default endpoint a
        # SELECT_CONFIGURATION or SELECT_INTERFACE asks for, counted when the
        # URB is parsed, ends either opened (a pipe a function driver can use)
        # or refused, each counted at its own site.
        'identity select endpoints requested == endpoints opened + select endpoints refused'
    )

    Groups = @(

        # -------------------------------------------------------------------
        @{
            Name = 'audio'
            Description = 'The isochronous path. Its own group because on Windows 98 this device bugchecks the guest, and a group boundary is the blast radius.'
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-audio/fs'
                    Model = 'usb-audio'
                    AddArgs = 'audiodev=matrixaud'
                    Settle = 35
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    # As matrix.psd1: an unattended run plays nothing, so the
                    # isochronous counters are structurally zero here, and the
                    # 2a line is USBAUDIO.VXD's fault after one URB.  Guest
                    # facts, carried unchanged.
                    ExpectByTarget = @{
                        '2b' = @(
                            'inert iso packets answered because nothing in an unattended run plays audio - batch 9-V needed a tone and a wav capture, and an idle usb-audio endpoint moves no isochronous traffic at all'
                            'zero iso missed service errors'
                            'zero iso packet errors'
                        )
                        '2a' = @(
                            'inert iso packets answered because Windows 98 SE USBAUDIO.VXD faults after one URB - exonerated in batch 9-V through a UHCI control'
                        )
                    }
                    MayWedgeGuest = @('2a')
                }
            )
        }

        # -------------------------------------------------------------------
        @{
            Name = 'hid'
            Description = 'Human interface devices on a root port, at both speeds.'
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-kbd/hs'
                    Model = 'usb-kbd'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    # matrix.psd1's `zero endpoint speed mismatches` asserted
                    # that usbport's High-Speed lie cost nothing here.  The HCD
                    # tells no lie, so ExpectedSpeed asserts the truth instead.
                    ExpectedSpeed = 'HS'
                    Expect = @( 'advance endpoints opened >= 1' )
                }
                @{
                    Name = 'usb-kbd/fs'
                    Model = 'usb-kbd'
                    AddArgs = 'usb_version=1'
                    Settle = 20
                    # matrix.psd1 asserted `advance endpoint speed mismatches`
                    # at switch 0 (the lie, by construction) and `zero` at 1
                    # and 2 (the virtual hub).  Both forms leave: a Full Speed
                    # device on a root port is reported Full Speed.
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                }
                @{
                    Name = 'usb-mouse/hs'
                    Model = 'usb-mouse'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    ExpectedSpeed = 'HS'
                    Expect = @( 'advance endpoints opened >= 1' )
                }
                @{
                    Name = 'usb-mouse/fs'
                    Model = 'usb-mouse'
                    AddArgs = 'usb_version=1'
                    Settle = 20
                    # As usb-kbd/fs.
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                }
                @{
                    Name = 'usb-tablet/hs'
                    Model = 'usb-tablet'
                    AddArgs = 'usb_version=2'
                    Settle = 20
                    ExpectedSpeed = 'HS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExcludedOnTarget = @{
                        '2a' = 'installing this driver hangs QEMU on Windows 98 - 3 of 4 main-loop hangs measured; unresolved, and it is the vehicle rather than the miniport (the row reaches NODRIVER, so enumeration works)'
                    }
                }
                @{
                    Name = 'usb-wacom-tablet/fs'
                    Model = 'usb-wacom-tablet'
                    Settle = 20
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExcludedOnTarget = @{
                        '2a' = 'excluded with usb-tablet/hs - the same absolute-pointer install path, never taught to the image for that reason'
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
                    AddArgs = 'drive=matrixdrv,removable=on'
                    Settle = 30
                    ExpectedSpeed = 'HS'
                    Expect = @(
                        'advance endpoints opened >= 2'
                        'identity transfers submitted == transfers completed + transfers cancelled'
                    )
                }
                # usb-bot and usb-uas are SCSI host adapters with a scsi-hd as
                # their Child, each on its own drive (matrix.psd1 says why).
                # Both measured at 12 Mb/s on an xHCI port, whatever their row
                # names suggest about the class.
                @{
                    Name = 'usb-bot/fs'
                    Model = 'usb-bot'
                    Child = 'scsi-hd,bus={ID}.0,drive=matrixdrv2'
                    Settle = 30
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                }
                @{
                    Name = 'usb-uas/fs'
                    Model = 'usb-uas'
                    Child = 'scsi-hd,bus={ID}.0,drive=matrixdrv3,scsi-id=0,lun=0'
                    Settle = 30
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'measured NODRIVER on both legs of the second post-release run (2026-08-30, fresh Windows 98 SE guest, class taught): the adapter is addressed and neither Windows 98 SE nor NUSB 3.3 has a UAS class driver'
                        '2b' = 'measured NODRIVER on both legs of the first post-release run (2026-08-30, fresh Windows 2000 SP4 guest): the adapter is addressed, Windows raises a Found New Hardware wizard and has no UAS class driver to offer; the wizard does not block enumeration on Windows 2000'
                        'xp64-fresh' = 'a guess taken 2026-09-18, not yet measured: the UAS class driver (uaspstor.sys) arrived with Windows 8, so XP x64 has none'
                        'win7-fresh' = 'a guess taken 2026-09-18, not yet measured: the UAS class driver (uaspstor.sys) arrived with Windows 8, so Windows 7 has none'
                    }
                }
            )
        }

        # -------------------------------------------------------------------
        # PHASE 27'S ROWS - see the header.  Under the HCD no function driver
        # binds an external hub: the bus speaks the hub class itself (design
        # record 13 section 10), so `endpoints opened` - a pipe a function
        # driver selected - never moves for a hub, and the rows read their
        # claim from `hubs started by the bus`, which the hub class moves when
        # it arms the hub's status-change pipe (section 9.6, consequence 1).
        @{
            Name = 'hub'
            Description = 'An external hub on a root port - the topology path.'
            Pump = $true
            Rows = @(
                @{
                    Name = 'usb-hub/fs'
                    Model = 'usb-hub'
                    Settle = 30
                    ExpectedSpeed = 'FS'
                    ClaimLabel = 'hubs started by the bus'
                    # matrix.psd1's `zero topology: behind-hub refused - no
                    # record` was usbport opening a device the snoop never saw;
                    # the bus creates the record before it addresses, so it is
                    # retired.
                    Expect = @(
                        'advance hubs started by the bus'
                        'advance topology: hub descriptors folded'
                        'advance topology: hub slots marked'
                        'zero topology: hub descriptors malformed'
                        'zero topology: nodes dropped'
                    )
                }

                # The churn sequence is matrix.psd1's, device for device; its
                # comments there say why each step is what it is.  Every device
                # in it is Full Speed: QEMU's usb-hub is a Full-Speed hub, and a
                # usb-mouse with no usb_version behind it measured 12 Mb/s.
                #
                # matrix.psd1's switch forms leave with the switch.  At 0 they
                # asserted `TT pairs disagreeing with usbport >= 10`, the
                # phantom TT usbport named for an FS hub on a root port the
                # miniport reported High Speed; at 1 and 2 they asserted the
                # virtual hub's extra tier - ten addressed and not eleven, the
                # tier-5 mouse out of reach.  Without the virtual hub the
                # five-tier chain is five deep in Windows' view again and the
                # tier-5 mouse is addressed (roadmap 27-V.1): eleven devices,
                # the first hub on the root port and ten behind hubs.  Derived
                # from the Steps, to be confirmed by the first run that serves
                # hubs.  `TT pairs programmed` stays a line, inert, because
                # QEMU models no High-Speed hub for a TT to be programmed for.
                @{
                    Name = 'usb-hub/churn'
                    Model = 'usb-hub'
                    Settle = 25
                    Steps = @(
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch1,bus=xhci.0,port={PORT}.1'; Wait = 20 }          # 1
                        @{ Do = 'del'; Id = 'ch1'; Wait = 12 }
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch1,bus=xhci.0,port={PORT}.1'; Wait = 20 }          # 2 - same port
                        @{ Do = 'del'; Id = 'ch1'; Wait = 12 }
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch1,bus=xhci.0,port={PORT}.3'; Wait = 20 }          # 3 - moved
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch2,bus=xhci.0,port={PORT}.2,ports=8'; Wait = 20 }    # 4
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch3,bus=xhci.0,port={PORT}.2.1'; Wait = 20 }        # 5
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch4,bus=xhci.0,port={PORT}.1,ports=8'; Wait = 15 }    # 6
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch5,bus=xhci.0,port={PORT}.1.1,ports=8'; Wait = 15 }  # 7
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch6,bus=xhci.0,port={PORT}.1.1.1,ports=8'; Wait = 15 }# 8
                        @{ Do = 'add'; Spec = 'usb-hub,id=ch7,bus=xhci.0,port={PORT}.1.1.1.1,ports=8'; Wait = 15 } # 9
                        @{ Do = 'add'; Spec = 'usb-mouse,id=ch8,bus=xhci.0,port={PORT}.1.1.1.1.1'; Wait = 20 }  # 10
                    )
                    ExpectedSpeed = @{ FS = 11 }
                    ClaimLabel = 'hubs started by the bus'
                    Expect = @(
                        'advance hubs started by the bus'
                        'advance devices addressed == 11'
                        'advance topology: behind-hub devices addressed == 10'
                        'advance topology: hub descriptors folded >= 2'
                        'advance topology: behind-hub opens >= 2'
                        'zero topology: hub descriptors malformed'
                        'zero topology: nodes dropped'
                        'zero topology: behind-hub refused - too deep'
                        'inert topology: TT pairs programmed because QEMU models no High-Speed hub, so no transaction translator exists to program (roadmap 27-V.1)'
                    )
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
                @{
                    Name = 'usb-net/fs'
                    Model = 'usb-net'
                    AddArgs = 'netdev=matrixnet'
                    NeedsNetdev = $true
                    Settle = 30
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'measured NODRIVER on the carried-along 2a image; neither Windows 98 SE nor NUSB 3.3 ships a driver for a CDC/RNDIS Ethernet function'
                        '2b' = 'measured NODRIVER on the carried-along 2b image; Windows 2000 SP4 ships no RNDIS or CDC Ethernet class driver'
                        'xp64-fresh' = 'measured 2026-09-19 (roadmap 22.9): Device Manager shows RNDIS/QEMU USB Network Device under Other devices, Code 28, no in-box INF matches USB\VID_0525&PID_A4A2'
                        'win7-fresh' = 'measured 2026-09-19 (roadmap 22.9): Device Manager shows RNDIS/QEMU USB Network Device under Other devices, Code 28, no in-box INF matches USB\VID_0525&PID_A4A2'
                    }
                }
                @{
                    Name = 'usb-serial/fs'
                    Model = 'usb-serial'
                    AddArgs = 'chardev=matrixchr1'
                    NeedsChardev = 1
                    Settle = 25
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'no class driver for a vendor-class serial adapter on Windows 98 SE or in NUSB 3.3'
                        '2b' = 'no class driver for a vendor-class serial adapter on Windows 2000 SP4'
                        'xp64-fresh' = 'a guess taken 2026-09-18, not yet measured: QEMU usb-serial is an FTDI vendor-class device and XP x64 ships no FTDI driver'
                        'win7-fresh' = 'a guess taken 2026-09-18, not yet measured: QEMU usb-serial is an FTDI vendor-class device and Windows 7 ships no FTDI driver in the box'
                    }
                }
                @{
                    Name = 'usb-braille/fs'
                    Model = 'usb-braille'
                    AddArgs = 'chardev=matrixchr2'
                    NeedsChardev = 2
                    Settle = 25
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2a' = 'no driver for a Baum braille display on Windows 98 SE or in NUSB 3.3'
                        '2b' = 'no driver for a Baum braille display on Windows 2000 SP4'
                        'xp64-fresh' = 'a guess taken 2026-09-18, not yet measured: no driver for a Baum braille display on XP x64'
                        'win7-fresh' = 'a guess taken 2026-09-18, not yet measured: no driver for a Baum braille display on Windows 7'
                    }
                }
                @{
                    Name = 'usb-ccid/fs'
                    Model = 'usb-ccid'
                    Settle = 25
                    ExpectedSpeed = 'FS'
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
                    ExpectedSpeed = 'FS'
                    Expect = @( 'advance endpoints opened >= 1' )
                    ExpectNoDriver = @{
                        '2b' = 'measured NODRIVER on the carried-along 2b image; a HID with no boot interface that Windows 2000 did not claim'
                    }
                }
            )
        }
    )
}
