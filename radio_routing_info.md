
- Type, When, VC, Radio 
### Downlinked Info 
- Perovskite Readings (array of f32s), upon request by uplink, 0, SBAND
- Images (jpg), upon request by uplink, 1, SBAND 
- Telemetry, every ten min (set a freertos timer, then send the command to cfdp in the callback), 2, UHF 
- ACKs/NAKs, when CFDP sends them, 3, UHF 
### Uplinked Info 
- Uplink Commands, when uplinked, 4, UHF 
