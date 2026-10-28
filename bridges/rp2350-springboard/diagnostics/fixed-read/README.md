# CS1 Fixed E2 — standalone PalmOS RAM diagnostic

Build: from the isolated worktree, run the existing
`/home/mozzwald/nio/fujinet-palm/docker/palm-build.sh make -C bridges/rp2350-springboard/diagnostics/fixed-read`.
The local `fujinet-palm-toolchain` image is used. No ROM image changes or device
installation occur during build. Generated `build/` is ignored.

App identity: `CS1 Fixed E2`, appl/FNFR, title `CS1 Fixed E2 RAM`, version
E2.20261028.1. It does not replace the existing Module B app or Setup.

Requires the already-installed Module B success preference (FNMC/1, format1,
active successful stage7); it obtains the module card/generation there rather
than assuming card1. HsCardAttrGet selector6 attributes3/4 return UInt32 CS0 base
and individual select size; CS1 is their checked sum. This diagnostic restricts
the observed16MiB-aligned Prism mapping and330ns setting, matching the present
physical-address ROM decoder. Missing receipt, unsupported mapping or non-RAM
execution blocks reads. It never changes timing, resets the Prism or executes CS1.

Launch performs API/identity/RAM-heap queries only, no explicit Springboard read.
Buttons require an active compatible firmware and a separate bench cue:

- CS1 once: one16-bit read at CS1 offset0, expectedBEEF.
- CS1 x100:10016-bit reads, allBEEF, no intentional inter-read delay.
- Alt x100:100iterations of CS0+8=FEED then CS1+0=BEEF, changing expected data.
- ROM pair x4:four explicit MOVE.L loads at aligned CS0+8, expectedFEEDBEEF;
  usable with Experiment1 as a bounded regression check. Never use the CS1
  buttons with Experiment1 or msc-003 firmware.

`access.S` forces MOVE.W and MOVE.L and reports a PC-relative RAM address.
Code1 heap bounds/card0 checks and the Handspring bus-error/removal catch list
protect each bounded operation. Tests save96-byte records to `FN Fixed Read E2
Log`, DATA/FNFR; `decode.py` independently checks identity, CRC, mapping, counts
and values. There is no NIO status, peripheral write, interrupt or command API.

Future bench tools (do not run until a test is explicitly authorized):

```
uv run --offline --with pyserial python bench.py install --prc /absolute/cs1-fixed-e2.prc --output /absolute/NEW-install
uv run --offline --with pyserial python bench.py collect --output /absolute/NEW-download
```

Both use verified serial palm-sync through existing `prism_collect.wait_palm`.
The engineer runs the tools and cues the user to press HotSync. Install uploads
only the standalone PRC; its header has no reset-after-install bit. Collect is
download-only and explicitly fails on missing databases. No pilot-link, raw USB,
RP upload, automatic app launch, reset or reinsertion. No collector runs merely
by preparing these files. `verify.py` checks PRC metadata and actual opcode bytes.

Physical execution is UNTESTED. Word-loop spacing is not a measured bus gap;
passing display/log data alone does not establish analog output-enable margin.
