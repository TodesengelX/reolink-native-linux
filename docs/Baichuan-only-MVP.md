**MVP Plan**

**Goal:** Baichuan-only Lumus/E1-class cameras can be discovered, manually added, authenticated, persisted, and viewed live over TCP `9000`, without requiring HTTP/HTTPS/RTSP/ONVIF.

1. **Protocol Foundation**
   - Extract shared Baichuan framing/login code from `BaichuanClient` and `BaichuanControl` into a small reusable session/helper.
   - Add a no-credential `probe()` that performs only the nonce/hello exchange to confirm a real Baichuan endpoint.
   - Add a credentialed `login()` result with structured failure: `ok`, `transport`, `auth`, `locked/too many attempts if detectable`, `protocol`.
   - Apply the same 31-character password normalization to Baichuan paths that HTTP already applies.
   - Add a concise provenance note beside each new Baichuan protocol/schema implementation, naming the source class: MIT `reolink_aio`, official Reolink documentation, or observed wire behavior. Do not port from AGPL `neolink`.

2. **Discovery**
   - Change `DeviceDiscovery` from `portOpen(9000) && confirmHttp()` to:
     - Try HTTP confirmation first for existing behavior.
     - If HTTP fails, try Baichuan nonce probe.
     - Emit `deviceFound(ip, "baichuan")` for Baichuan-only devices.
   - Keep the probe unauthenticated so scanning does not consume failed-login attempts.

3. **Persistence / Device Model**
   - Add an explicit host transport/access field, likely `transport TEXT NOT NULL DEFAULT 'http'`, values: `http`, `baichuan`.
   - Avoid overloading `kind`; keep `kind` as `camera`, `nvr`, `stream`.
   - Update DB migration, `HostRecord`, add/update/select paths, and sidebar/device info display.
   - Existing saved devices default to `http`.

4. **Manual Add / Test Device**
   - Update Add Device flow so discovered `baichuan` devices default to port `9000` and Baichuan transport.
   - For manually entered port `9000`, try HTTP first only if requested, then Baichuan fallback.
   - Update `DeviceManager::testDevice()`:
     - HTTP path remains unchanged.
     - Baichuan path logs in via Baichuan and parses minimal device info from login reply or `cmd_id=80`.

5. **Validation**
   - Update `validateAsync()` to branch by transport.
   - For Baichuan-only MVP, create one channel:
     - `channel = 0`
     - `online = true`
     - codec can be blank so stream probing decides, or default `h264` for Lumus E430.
     - capabilities: audio/floodlight/siren can start conservative or be parsed later.
   - Do not create an HTTP `ReolinkHttpClient` for Baichuan-only entries.

6. **Live View Routing**
   - Update `liveUrl()` callers / QML behavior so Baichuan-only devices do not expect RTSP URLs.
   - Route live view directly through `startBaichuanLive(row, player, mainStream)`.
   - For Baichuan-only rows, make both SD/HD modes use Baichuan initially; choose `mainStream=false` for grid/fluent and `true` for focused/clear.
   - Verify `StreamPlayer` can probe Baichuan H.264 cleanly when expected format is empty.

7. **MVP Feature Gating**
   - Disable or hide HTTP-dependent features for Baichuan-only rows:
     - snapshots via HTTP
     - recording search/download
     - detection polling
     - host-level settings
   - Keep existing Baichuan settings controls only where they already work and fail gracefully otherwise.
   - Avoid making polling mark Baichuan-only devices unreachable just because there is no HTTP client.

8. **Tests**
   - Add unit tests for Baichuan crypto/password normalization if missing.
   - Add parser tests for login/device-info XML.
   - Add DB migration test for `transport`.
   - Add `DeviceManager`-level tests or focused seams for:
     - HTTP device still validates normally.
     - Baichuan-only validation creates one online channel.
     - failed Baichuan login reports auth/protocol instead of unreachable where possible.

9. **Manual Verification**
   - `--discover` shows the Lumus E430 as `baichuan`.
   - Add dialog can select discovered camera, authenticate, persist, restart app, and still show it.
   - Live grid starts Baichuan sub/main stream without RTSP.
   - Bad password does not trigger repeated auto-retries.
   - Existing HTTP/NVR devices still work unchanged.

10. **Baichuan Camera Settings Read Path**
    - Define a small command/schema registry for settings supported by the target
       Lumus/E1-class cameras, including command id, XML request shape, response
       tags, channel scope, and whether a setting is safe to expose.
    - Reuse `BaichuanControl` for authenticated GET transactions and expose a
       structured result containing `ok`, `transport`, `protocol`, `status`, and
       parsed fields rather than leaking raw XML into QML.
    - Implement reads for the first useful camera controls: image tuning
       (brightness, contrast, saturation, sharpness), mirror/flip, day/night
       mode and threshold, detection sensitivity, recording pre-record/delay,
       and weekly recording schedule.
    - Preserve unknown XML fields and tolerate firmware-specific omissions so a
       newer or older camera does not make the whole settings page fail.
    - Mark each command/schema with provenance from MIT `reolink_aio`, official
       Reolink documentation, or observed wire behavior. Do not port from AGPL
       `neolink`.

11. **Baichuan Camera Settings Write Path**
    - Use read-modify-write for every setting: fetch the current command payload,
       change only the requested leaf fields, and submit the matching Set command.
    - Validate ranges, enum values, boolean fields, schedule dimensions, and
       channel ownership before sending; reject invalid values locally.
    - Serialize writes per camera/channel and refresh the affected setting after
       success so the UI reflects the device's normalized value.
    - Return structured auth, locked, transport, protocol, and device-rejected
       errors; never retry writes automatically after an auth or locked failure.
    - Disable unsupported controls per capability/model and keep HTTP-only
       settings hidden for Baichuan transport rows.
    - Add UI loading, success, and failure states for settings reads and writes;
       do not leave stale controls appearing successfully changed after a failed
       write.

12. **Recorded Search and Time Conversion**
    - Add Baichuan recording search for a selected channel and local calendar
       day, returning normalized segments with start/end epoch, recording type,
       and camera name.
    - Parse the camera/NVR playback clock and recording-file boundaries, keeping
       the conversion between wall-clock Unix time and Baichuan playback time in
       one tested helper.
    - Handle empty days, overlapping segments, midnight boundaries, timezone
       changes, malformed records, and cameras whose clock differs from the host.
    - Keep HTTP recording search as the existing path and select Baichuan search
       strictly from the persisted transport field.

13. **Native Recorded Clip Playback and Export**
    - Start Baichuan recorded playback directly into `StreamPlayer` using the
       selected file boundary and playback offset; do not require an RTSP or
       HTTP-FLV URL for Baichuan-only rows.
    - Support main/sub stream selection, H.264/H.265 probing, keyframe startup,
       stop, reconnect, and in-place seek without opening duplicate sessions.
    - Expose playback errors distinctly from authentication and transport errors,
       and release the Baichuan session when leaving playback or switching rows.
    - Add a native export path that writes the selected recording to a local
       media file, preserving the original elementary stream where possible;
       make cancellation and partial-file cleanup explicit.
    - Keep snapshot, detection polling, and other HTTP-dependent features gated
       off for Baichuan-only devices until equivalent Baichuan implementations
       exist.

14. **Settings / Playback Tests**
    - Add fixture-based tests for each supported settings XML response and for
       read-modify-write payload preservation, including unknown tags and omitted
       optional fields.
    - Add validation tests for ranges, schedules, per-channel addressing, write
       serialization, and auth/locked/protocol failure classification.
    - Add recording parser tests for empty, boundary, overlapping, malformed,
       and clock-offset cases, plus wall-clock/playback-time round trips.
    - Add `DeviceManager` seam tests proving Baichuan rows never construct an
       HTTP client, select Baichuan search/playback, and produce one stream
       session that can seek and stop cleanly.
    - Add media tests for H.264/H.265 probing, truncated clips, missing keyframes,
       and export cleanup.

15. **Expanded Manual Verification**
    - Read settings from a Baichuan-only camera after restart and confirm values
       match the device rather than cached defaults.
    - Change image, day/night, detection, and recording schedule settings; verify
       the device applies only the requested fields and the UI refreshes them.
    - Search a day containing recordings, play both stream modes, scrub across a
       file boundary, stop playback, and reopen the same clip.
    - Export a clip, cancel an export, and verify no unusable partial file is
       left behind.
    - Confirm bad credentials do not trigger repeated setting writes or playback
       login attempts, and confirm existing HTTP/NVR settings and playback remain
       unchanged.

**Estimate**
Discovery/add/login/live-view MVP: `4-7 engineering days`.

Settings and recorded-playback expansion: `5-10 engineering days`, depending
on the number of camera firmware variants that need wire/schema fixtures.

The risky parts are command/schema variation between firmware versions, safe
read-modify-write behavior, camera clock conversion, and keeping native
playback sessions bounded while the UI switches between live view, search, and
export.
