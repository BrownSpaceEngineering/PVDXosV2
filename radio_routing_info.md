
- Type, When, VC, Radio 
### Downlinked Info 
- Perovskite Readings (array of f32s), upon request by uplink, 0, SBAND
- Images (jpg), upon request by uplink, 1, SBAND 
- Telemetry, in UHF beacon bursts (one telemetry packet per frame, without CFDP), 2, UHF 
- ACKs/NAKs, when CFDP sends them, 3, UHF 
### Uplinked Info 
- Uplink Commands, when uplinked, 4, UHF 
### Both Ways
- PUSP directives (UHF link handshakes, without CFDP), during link setup and turn handovers, 5, UHF
