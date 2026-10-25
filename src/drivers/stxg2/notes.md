# Notes on the STXG2 Driver

- We will always stay in MODCOD 0 or 1
  - It's worth considering if we switch to MODCOD 1 after starting in MODCOD 0, provided our link accomodates.
    these settings have the same encoded rate and bandwidth requirement, so it might be a good idea.
- We will always use the lowest TX power available
- We're using our own internal framing (USLP), but this shouldn't interfere with PHY reception in the downconverter
- Please do not the cat