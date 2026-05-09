# CFDP Master List

## CFDP TODO
 - Make sure we reject incoming files with large file flag set (might already happen since we'll never have a buff that big)
 - Make it so unacknowledged transaction are actually run in that mode
 - Implement timers with rtc
 - Finish timer implmentation 
 - Implement MRAM flushing to preserve transaction state on boot  
 - more ...

## Pitfall Master List
 - The missing-chunk list (NAK ring buffer) can index the wrong slot, so it may read or write outside valid memory.
 - File buffers are handed out without being marked “in use,” two transfers can accidentally share one buffer.
 - Finished-PDU parsing puts data into the wrong struct field.
 - Header parsing assumes a fixed 16-byte header, but CFDP headers can vary based on field lengths. (see cfdp_pdu.c line 31)
 - Unacknowledged mode is not fully implemented; some logic still behaves as if acknowledgments are always used.
 - NAK packets can become too large for one frame when many gaps exist.
 - Timing not  based on real RTC elapsed time.
 - Incoming packet lengths are not fully cross-checked against actual received size, so malformed packets can be misread.
 - The slot-free flag is a single boolean gate that can get out of sync with actual slot usage if any path misses an update.
 - function headers

 ## CFDP Timers
- Updated cfdp to user FreeRTOS timers to handle CFDP Timers. 
- Currently in process implementation of timer callbacks
- need to update existing code to correcty call/use new timers 