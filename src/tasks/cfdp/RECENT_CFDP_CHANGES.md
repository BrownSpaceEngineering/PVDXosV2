#ENUM Master List

CFDP TODO
 - Make sure we reject incoming files with large file flag set (might already happen since we'll never have a buff that big)
 - Make it so unacknowledged transaction are actually run in that mode
 - Implement timers with rtc
 - more ...

#Pitfall Master List
 - The missing-chunk list (NAK ring buffer) can index the wrong slot, so it may read or write outside valid memory.
 - File buffers are handed out without being marked “in use,” two transfers can accidentally share one buffer.
 - Finished-PDU parsing puts data into the wrong struct field.
 - Timing is fake right now (hardcoded value) instead of based on real RTC elapsed time.
 - Header parsing assumes a fixed 16-byte header, but CFDP headers can vary based on field lengths.
