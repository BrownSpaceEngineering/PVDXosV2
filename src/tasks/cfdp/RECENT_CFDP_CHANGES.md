# CFDP Master List

## CFDP TODO
 - Make sure we reject incoming files with large file flag set (might already happen since we'll never have a buff that big)
 - Make it so unacknowledged transaction are actually run in that mode
 - Implement MRAM flushing to preserve transaction state on boot  
 - more ...

## Pitfall Master List
 - The missing-chunk list (NAK ring buffer) can index the wrong slot, so it may read or write outside valid memory.
 - Unacknowledged mode is not fully implemented; some logic still behaves as if acknowledgments are always used.
 - NAK packets can become too large for one frame when many gaps exist.
 - Incoming packet lengths are not fully cross-checked against actual received size, so malformed packets can be misread.
 - The slot-free flag is a single boolean gate that can get out of sync with actual slot usage if any path misses an update.
 - function headers
 - actually handle checksum matching once filedata is complete. 
 - look at parse metadata pdu

 ## CFDP Timers
- Updated cfdp to user FreeRTOS timers to handle CFDP Timers. 
- Implemented timer timeout functionality and updated existing timer code to support it 
- TODO: I don't think we actually need to store a nak and ack timer b/c only one will ever be used at once. 