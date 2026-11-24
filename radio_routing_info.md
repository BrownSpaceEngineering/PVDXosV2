- Type, When, VC, Radio

"Pass radio" depends on the pass the ground opened with its SYN (`PUSP_NOTIF_SBAND_PASS`): S-band on a UHF + S-band
pass, UHF on a UHF-only pass. On an S-band pass the ground keeps the UHF turn for the whole pass: uplink stays on UHF and
every CFDP PDU goes down on S-band as soon as CFDP sends it (full duplex). If S-band fails, the transaction is killed,
not moved to UHF.

### Downlinked Info
- Perovskite Readings (array of f32s), upon request by uplink, 0, pass radio
- Images (jpg), upon request by uplink, 1, pass radio
- Telemetry, in UHF beacon bursts (one telemetry packet per frame, without CFDP), 2, UHF
- ACKs/NAKs/FINs, when CFDP sends them, 3, pass radio
### Uplinked Info
- Uplink Commands, when uplinked, 4, UHF (CFDP's replies to them: 4, pass radio)
### Both Ways
- PUSP directives (UHF link handshakes, without CFDP), during link setup and turn handovers, 5, UHF

S-band framing (TODO(temp), to be agreed with the ground decoder): each USLP frame is sent after the CCSDS attached sync
marker `0x1ACFFC1D` and filled with `0x00` to a multiple of 4 bytes.
