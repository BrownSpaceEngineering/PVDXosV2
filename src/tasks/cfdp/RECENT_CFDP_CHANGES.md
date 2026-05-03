# Recent CFDP Uncommitted Changes

This file summarizes the latest uncommitted changes currently made in the CFDP section under `src/tasks/cfdp/`.

## Files Touched

- `cfdp_engine.c`
- `cfdp_engine.h`
- `cfdp_main.c`
- `cfdp_task.c`
- `cfdp_task.h`

## High-Level Summary

The recent CFDP work primarily expands the protocol state machines, fixes PDU construction details, and improves transaction lifecycle handling for both send and receive paths.

## Detailed Change Summary

### 1. Send-side state machine improvements

- Added `next_seq_num()` implementation in `cfdp_engine.c` with a note that the counter is currently RAM-backed and should eventually move to MRAM persistence.
- Fixed send-side EOF handling so reliable transfers now move into `CFDP_SEND_STATE_WAIT_ACK` before `WAIT_FIN`, instead of skipping directly to finish.
- Added timeout and retransmission handling for EOF acknowledgements.
- Added timeout handling while waiting for `Finished` PDUs from the receiver.
- Added explicit terminal error handling comments and cleaner send-state transitions.

### 2. Receive-side state machine improvements

- Added NAK timer progression during file reception so the receiver can request missing data if file-data PDUs stop arriving.
- Added retry-limit handling for NAK retransmissions.
- Added reliable closeout logic for sending `Finished` PDUs and waiting for ACK-of-FIN.
- Added the new receive state `CFDP_RECV_STATE_WAIT_FIN_ACK`.
- Added explicit `DONE` and `ERR` terminal-state handling in the receive state machine.

### 3. Transaction storage and lifecycle updates

- Improved `cfdp_alloc_transaction()` so `slot_free` is recomputed correctly when a slot is allocated.
- Added `cfdp_free_transaction()` to release transaction slots cleanly.
- Updated `cfdp_main.c` so the main CFDP task loop skips inactive transactions, checks the `cfdp_transact()` result, logs completion/error, and frees finished transactions.

### 4. PDU construction fixes and new helpers

- Fixed Metadata PDU construction so the directive code is included correctly in the PDU data field.
- Fixed destination filename encoding in Metadata PDU generation.
- Fixed EOF PDU construction so the directive code is included correctly and field offsets are aligned properly.
- Added `cfdp_send_fin()` to build and send `Finished` PDUs.
- Added `cfdp_send_ack()` to build and send ACK PDUs.
- Updated header declarations in `cfdp_engine.h` for the new CFDP helper functions.

### 5. Incoming PDU processing expanded

- Expanded `cfdp_process_pdu()` in `cfdp_task.c` to handle:
  - Metadata PDUs by creating new receive transactions.
  - File-data PDUs by parsing offsets/payloads and writing data into the receive buffer.
  - EOF PDUs by checking for missing segments and validating checksums.
  - ACK PDUs by advancing sender/receiver closeout states.
  - Finished PDUs by ACKing them and completing send-side transactions.
  - NAK PDUs by queueing retransmission requests on the send side.
- Added logic to maintain a received-segment bitmap instead of inferring completeness from file contents.

### 6. Transaction creation cleanup

- Cleaned up `cfdp_put_request()` initialization in `cfdp_task.c`.
- Replaced the previous large compound initializer with explicit field assignment through a transaction pointer.
- Fixed transaction cancel logic to compare against `transaction_id.seq_num`.
- Normalized initial filename pointers/lengths to null/zero until real filename handling is wired in.

### 7. CFDP data structure updates

- Added placeholder file-size macros in `cfdp_task.h`:
  - `IMAGE_FILE_SZ`
  - `TELEMETRY_FILE_SZ`
- Added receive bitmap sizing macros:
  - `CFDP_MAX_SEGMENTS`
  - `CFDP_BITMAP_WORDS`
- Added `received_bitmap` storage to `cfdp_transaction_t` for gap tracking.

### 8. Header comments and TODO status updates

- Updated the comment in `cfdp_engine.h` describing `next_seq_num()`.
- Marked several CFDP TODOs as done in `cfdp_engine.h`, including:
  - transaction storage ownership/lifecycle
  - MRAM persistence decision note
  - segmentation policy
  - `process_pdu()` state updates
  - directive-code inclusion fixes in send helpers

## Net Effect

These changes move the CFDP implementation closer to a usable end-to-end transfer engine by improving:

- reliable send/receive closeout behavior
- retransmission and timeout behavior
- receive-side gap tracking
- transaction cleanup and slot reuse
- correctness of Metadata, EOF, Finished, and ACK PDU formatting
- processing of incoming CFDP PDUs

## Scope Note

This summary only covers the latest uncommitted changes in the CFDP area under `src/tasks/cfdp/`. It does not summarize unrelated uncommitted files elsewhere in the repository.