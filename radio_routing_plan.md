# Plan: radio routing for the CFDP / UHF stack

Implements the requirements in `radio_routing_info.md`. The SBAND task doesn't exist yet, so anything routed to
SBAND is treated as unimplemented. When this plan is done, the radio stack should work end to end over UHF.

Several existing bugs stop CFDP receive and buffer allocation from ever working, separate from the routing work,
so Phase 0 fixes those first.

## Target routing

| Stream | Sent when | VC | Radio | CFDP role |
|---|---|---|---|---|
| Perovskite readings | Uplink requests it | 0 | SBAND | Outgoing file (rejected for now: SBAND not implemented) |
| Images | Uplink requests it | 1 | SBAND | Outgoing file (rejected for now) |
| Telemetry | 10-minute FreeRTOS timer | 2 | UHF | Outgoing file: Metadata, FileData, EOF |
| ACK / NAK (and FIN) | Whenever CFDP sends them | 3 | UHF | Reply PDUs |
| Uplink commands | When uplinked | 4 | UHF | Incoming file, parsed and dispatched when the transfer completes |

**How PDUs are routed:** a PDU that carries a file (Metadata, FileData, EOF, and resends) goes out on its
transaction's stream. Every reply PDU (ACK, NAK, metadata NAK, FIN, stale ACK, reject FIN) goes on VC 3.

## Phase 0: existing bugs that block UHF

1. **Receive never works.** `cfdp_recv` (`src/tasks/cfdp/cfdp_utils.c:59`) rejects any buffer smaller than
   `RADIO_DOWNLINK_MAX_SIZE + 2` (534 bytes). `cfdp_main.c:89` passes `TXN_FRAME` (512), so every call returns -1.
   `main_cfdp` also treats a return of `0` as success (0 actually means the queue was empty) and passes `TXN_FRAME`
   rather than the real length.
   - Fix: `int n = cfdp_recv(recv_buff, sizeof(recv_buff)); if (n > 0) cfdp_process_pdu(recv_buff, n);`
2. **Uplink payload length is off by 14 bytes.** `cfdp_recv` copies `data_length + 1` bytes, but that length also
   counts the 14-byte secondary header. Subtract `SPP_SECONDARY_HEADER_SIZE` when the flag is set. Also check that
   APID == `CFDP_APID` and that the packet type is a telecommand.
3. **CFDP buffer mutexes are never created.** `cfdp_small_buffs[i].header.semaphore`,
   `cfdp_large_buff.header.semaphore` and `header.buff` are never initialised. Create the mutexes statically in
   `init_cfdp()` and set `.buff` to point at each buffer.
4. **`cfdp_alloc_small_buff` never succeeds** (`cfdp_utils.c:213-215`). It sets `ref_count = 1` and then tests
   `!ref_count`. **`cfdp_alloc_large_buff`** returns `&cfdp_large_buff.buff` instead of `&cfdp_large_buff.header`.
   Both callers in `cfdp_task.c:404,641` then dereference a NULL pointer; add NULL checks before using `->buff`.
5. **Every send transaction shares timer slot 0** (`cfdp_task.c:52,73-74`). `cfdp_put_request` `memset`s the
   transaction, erasing the per-slot timer handles that `cfdp_alloc_transaction` set, then hard-codes `[0]`. Remove
   both the memset and the hard-coded handles.
6. **Outgoing transfers are addressed to ourselves.** `dest_entity_id = ENTITY_ID_SPACECRAFT` should be
   `ENTITY_ID_GROUND`.
7. **Timeouts can deadlock in `main_cfdp`.** The loop `while (slot_free && xQueueReceive(...))` stops reading
   commands once every slot is full, and timer timeouts arrive as commands. Always drain the command queue;
   `cfdp_put_request` already handles having no free slot. Use a short `CFDP_POLL_MS` block time instead of the full
   watchdog block time, so incoming PDUs get serviced promptly.
8. **Send failures are ignored, so data is silently lost.** All `cfdp_send(...)` return values in `cfdp_pdu.c` are
   discarded, and the downlink queue holds only 4 items. In `CFDP_SEND_STATE_FILE_SEND`, `file_offset` advances even
   when the enqueue failed, and the `do { } while (IN_PROGRESS)` loop at `cfdp_main.c:102` pushes a whole file in one
   go. Fix:
   - Have `cfdp_send_*` return the send result.
   - On a full queue, move state or offset forward only after a successful send, and return `CFDP_RESULT_BLOCKED`
     so the next loop retries.
   - Let reply PDUs block briefly, about 100 ms, well under the watchdog timeout.
9. **The UHF radio task waits twice per loop** (`src/tasks/uhf_radio/uhf_radio_main.c:98,105`): once on the command
   queue and once on the downlink queue, each for the full block time. Use a timeout of 0 or a few ticks on the
   command queue, or use a queue set (`configUSE_QUEUE_SETS` is already 1).

## Phase 1: routing table (new `src/ccsds/radio_routing.h`)

```c
typedef enum { RADIO_UHF, RADIO_SBAND } radio_id_t;
typedef enum {
    RADIO_STREAM_PEROVSKITE = 0,  // VC 0, SBAND
    RADIO_STREAM_IMAGE,           // VC 1, SBAND
    RADIO_STREAM_TELEMETRY,       // VC 2, UHF
    RADIO_STREAM_CFDP_REPLY,      // VC 3, UHF
    RADIO_STREAM_UPLINK_CMD,      // VC 4, UHF
    RADIO_STREAM_COUNT,
} radio_stream_t;
typedef struct { uint8_t vcid; radio_id_t radio; } radio_route_t;
extern const radio_route_t radio_routes[RADIO_STREAM_COUNT];

#define RADIO_GMAP_ID(vcid) (((uint32_t)USLP_TFVN << 26) | ((uint32_t)CCSDS_SCID << 10) | ((vcid) << 4) | RADIO_DOWNLINK_MAP_ID)

// Enqueues on the right radio's downlink queue; SBAND returns -RADIO_ERR_NOT_IMPL with a warning()
int radio_route_send(radio_stream_t stream, const uint8_t *spp, uint16_t len, TickType_t wait);
```

- Add `uint8_t vcid` to `radio_queue_item_t` (`uhf_radio_task.h:26`). Remove the fixed `RADIO_DOWNLINK_VCID` /
  `RADIO_GMAP_ID`.
- In `main_uhf_radio`, call `uslp_mapp_request(..., RADIO_GMAP_ID(item.vcid), ...)`.
- On receive: after `uslp_transfer_frame_parse`, drop any frame whose VC isn't an accepted uplink VC, and copy
  `virtual_channel_id` into `item.vcid` before queuing it on `uplink_queue`.
- `radio_route_send` lives with the radio code (`src/tasks/cfdp/radio_routing.c`) so the SBAND branch can later
  point at the SBAND task's queue without changing CFDP.

## Phase 2: CFDP uses the routes

- Replace `cfdp_txn_type_t` (`cfdp_task.h:93`) with `radio_stream_t txn->stream` (or map the enum to it):
  `IMAGE` → `RADIO_STREAM_IMAGE`, `TELEMETRY` → `RADIO_STREAM_TELEMETRY`, and add `PEROVSKITE` and `UPLINK_CMD`.
- Change the signature to `int cfdp_send(void *buff, size_t sz, radio_stream_t stream)`, and have it call
  `radio_route_send` after SPP serialization.
  - `cfdp_send_metadata`, `cfdp_send_filedata`, `cfdp_send_eof` and `cfdp_resend` pass `txn->stream`.
  - `cfdp_send_ack`, `cfdp_send_nak`, `cfdp_send_metadata_nak`, `cfdp_send_fin`, `cfdp_send_reject_fin` and
    `cfdp_send_stale_ack` pass `RADIO_STREAM_CFDP_REPLY`.
- **Reject SBAND at put time.** In `cfdp_put_request`, check the route before allocating a slot: if
  `radio_routes[stream].radio == RADIO_SBAND`, log
  `warning("cfdp put: stream %d routed to SBAND, not implemented\n")` and return. A slot is never used up, and no
  transaction fails halfway through.
- **Tag incoming transactions.** Change `cfdp_recv` so it also returns the item's `vcid`, and pass that into
  `cfdp_process_pdu(raw, sz, vcid)`. When a new receive transaction is created (`cfdp_task.c:539,604`), set
  `new_txn->stream = UPLINK_CMD` for VC 4 and drop file transfers that arrive on any other VC.
- Keep a `CFDP_UNITTEST` hook: the `test_mem` memcpy stays in `cfdp_send`, so the existing CFDP tests still capture
  output (update the calls to the new signature).

## Phase 3: telemetry every 10 minutes (VC 2)

- New `src/telemetry/telemetry_timer.c`:
  - A static auto-reload timer,
    `xTimerCreateStatic("Telemetry", pdMS_TO_TICKS(TELEMETRY_PERIOD_MS = 600000), pdTRUE, ...)`, created in
    `init_cfdp()` and started when the CFDP task starts.
  - The callback runs on the 64-word timer task stack, so it only calls `enqueue_command()` (already non-blocking)
    with a new `OPERATION_CFDP_PUT_TELEMETRY` aimed at `p_cfdp_task`. It does no serialization.
- In the CFDP task's handler for that operation:
  - If the static `telemetry_file[TELEMETRY_DOWNLINK_SIZE]` still belongs to an active telemetry transaction, skip
    this cycle with a `warning`.
  - Otherwise call `telemetry_collect(&tel_downlink)` (new; fills what is available today and zeroes the rest), then
    `telemetry_downlink_serialize(...)`, then `cfdp_put_request` with `stream = RADIO_STREAM_TELEMETRY`.
  - The 512-byte file goes out as Metadata + 2 FileData + EOF on VC 2.
- `cfdp_request_data_t` is passed by pointer in `command_data_t`, so the put data must be static, not a local in the
  callback.

## Phase 4: uplink commands (VC 4) end to end

- When a receive transaction completes (`cfdp_main.c:105`) with `stream == RADIO_STREAM_UPLINK_CMD`, hand
  `txn->buff_header` to the uplink parser **before** `cfdp_free_transaction`. The buffer stays owned until the
  commands that refer to it are done.
- There are two parsers today:
  - `parse_uplink_packet` (`src/tasks/cfdp/uplink_commands.c`) is buggy. The callsign comparison reads `raw[5 - i]`
    (reversed) and only checks 5 bytes. `big_endian_to_uint16` reads `arr[2]`/`arr[3]`.
  - `telemetry_uplink_parse` (`src/telemetry/telemetry.c:346`) is tested.
  - **Recommendation:** parse with `telemetry_uplink_parse` and rewrite `uplink_commands.c` as a dispatcher that
    turns `tel_command_t` into `command_t`. For each command that refers into the buffer, call
    `cfdp_add_buff_reference`; consumers call `cfdp_release_buff`. Drop the old parser.
- `TAKE_PHOTO` / `SEND_PHOTO` (and a future perovskite request) become CFDP puts on SBAND streams, which Phase 2
  rejects with a "not implemented" warning.
- Parser mismatch: `telemetry_uplink_parse` expects a full SPP telecommand packet, while the CFDP file holds only the
  payload. Either the ground station puts an SPP packet inside the file, or a version that parses just the payload
  (preamble + commands) gets split out. This needs a team decision (see open questions).

## Phase 5: finish the UHF radio (AT86RF215)

1. **Chip setup:** follow the TODO list at `uhf_radio_main.c:31-37`. Move `spi_m_sync_enable` and `at86_init_pvdx`
   into `init`/task start, then set frequency, TX power and PHY, and leave the radio in RX. Mark `at86_init_pvdx`
   `static` or declare it in the header.
2. **Receive:** replace the `at86_peek_frame` / `at86_pop_frame` stubs. Poll `BBC0_IRQS.RXFE`, read
   `RXFLL`/`RXFLH`, read the frame buffer at `FBRXS` with one SPI block read (same single-SELN pattern as
   `at86_write_tx_buffer`), and remove the 2-byte FCS from the length.
   - `IRQS` is cleared when read, and the TX path reads it too. Keep a shared `pending_irqs` variable so a TX poll
     doesn't swallow RXFE (or the reverse).
3. **12.5 kb/s PHY:** `AT86_MR_O_QPSK_100CH_12_5KB` should use `DRATE_MOD_1` (the existing TODO).
4. **Enable the tasks:** `cfdp_task` and `uhf_radio_task` both have `.enabled = false` in `src/tasks/task_list.c`.
   Enable them, or confirm the state machine turns them on.

## Phase 6: tests (`make test TESTS="cfdp spp ..."`)

- **Routing table:** each stream maps to the expected VC and radio; an SBAND put leaves the transaction store
  unchanged.
- **CFDP routing:** run one send transaction and check the `vcid` on each captured queue item. File PDUs should be
  on 2; ACK, NAK and FIN on 3.
- **Downlink round trip:** `cfdp_send` → USLP serialize → USLP parse → `cfdp_recv`. Lengths should match exactly,
  which covers the off-by-14 fix.
- **Backpressure:** with the downlink queue full, `file_offset` must not move forward.
- **Uplink:** a CFDP transfer on VC 4 completes and the dispatcher queues the expected commands; the same transfer
  on VC 2 is dropped.
- **Hardware:** after flashing is approved, check over RTT that telemetry goes out every 10 minutes and that a
  ground-sent command file is dispatched.

## Open questions

1. **Which VC do ground replies arrive on?** These are the ground's ACK/NAK/FIN for our telemetry transfers. The
   routing doc only lists VC 4 for uplink. Recommendation: accept VC 3 and VC 4 on uplink and send both to CFDP.
2. **Does FIN count as ACK/NAK traffic (VC 3)?** Assumed yes, since it's a reply PDU, but it could instead follow
   its transaction's VC.
3. **Uplink file format:** a raw SPP packet, or just preamble + commands (see Phase 4)?
4. **Telemetry mode:** acknowledged (current default) or unacknowledged? Acknowledged means ground passes must answer
   within `TRANSACTION_LIFETIME_MS` (20 s), and the retransmit timeout of `RETRANSMIT_TIMEOUT_MS = 100` is far
   shorter than one UHF frame time (about 0.7 s for a 532-byte packet at 6.25 kb/s). Both values need retuning
   either way.

## Suggested order

Phase 0, then 1, 2, 5, 3, 4, 6. Phases 0–2 can be tested without hardware; Phase 5 is required before any
over-the-air test.
