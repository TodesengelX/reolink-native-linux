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

**Estimate**
Discovery/add/login/live-view MVP: `4-7 engineering days`.

The risky parts are not discovery itself; they are cleanly separating “device identity/validation” from the existing HTTP client assumptions, and making the UI route live playback without first demanding an RTSP URL.
