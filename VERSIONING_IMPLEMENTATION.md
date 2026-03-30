# Protocol Versioning & Compatibility Implementation Plan

This document defines the two-phase implementation plan for ensuring safe, version-controlled communication between the Android Host (Master) and the RP2040 Firmware (Slave).

## Goal
To enforce a **Hard Failure** during connection initiation if the App and Firmware are incompatible, preventing dangerous operations caused by mismatched memory layouts or command sets.

---

## Phase 1: Semantic Versioning Handshake
*Target: Establish the communication foundation and basic runtime whitelisting.*

### 1. Firmware: Version Definition
- Create `include/version.h` to serve as the single source of truth for the firmware version.
- **Tasks**:
    - Define macros for `FW_VERSION_MAJOR`, `FW_VERSION_MINOR`, `FW_VERSION_PATCH`.
    - Update `CMakeLists.txt` and `Makefile` to reference these values or ensure they are synchronized during the build process.

### 2. Protocol: `GET_VERSION` Command
- Add a new command ID to the protocol.
- **Tasks**:
    - Update `include/serial_comm_manager.h`: `#define GET_VERSION 0x06`.
    - Update `PROTOCOL.md` to document the response format (a framed packet containing the SemVer string).
    - Implement the listener in `src/serial_comm_manager.c` to respond to `GET_VERSION` with the constants from `version.h`.

### 3. Android Host: Whitelist Enforcement
- The Android App becomes responsible for validating the connection.
- **Tasks**:
    - On USB-Serial connection, immediately send `GET_VERSION`.
    - Compare the received string against an internal **Compatibility Whitelist**.
    - **Logic**:
        - **Pass**: If Major version matches and Minor version is within the supported range.
        - **Fail**: If version is unrecognized or outside supported bounds.
    - **Action**: On failure, close the transport and display a "Firmware/App Update Required" message.

---

## Phase 2: Manifest-Based Validation
*Target: Decouple compatibility rules from the App's source code for easier management.*

### 1. Manifest Definition
- Move compatibility rules into a central JSON structure.
- **Tasks**:
    - Define `compatibility_manifest.json` containing mappings of App versions to supported Firmware version ranges.
    - Support "Blacklisting" specific faulty firmware versions.
    - Example structure:
      ```json
      {
        "app_version": "1.0.2",
        "compatible_firmware": {
          "min": "0.2.0",
          "max": "0.3.0",
          "blacklist": ["0.2.5"]
        }
      }
      ```

### 2. Android Host: Dynamic Parsing
- Update the App's validation logic to use the manifest.
- **Tasks**:
    - Implement a parser in the Android library to read the manifest (initially bundled as a local asset).
    - (Optional) Implement a HTTP GET request to check the manifest without a full App store release. Local asset will be used as a fallback in case the request fails
    - Replace the Phase 1 hardcoded whitelist logic with a lookup against this parsed data.

### 3. Lifecycle Synchronization
- Integrate manifest updates into the release workflow.
- **Tasks**:
    - Update the manifest whenever a new firmware version is released that changes the protocol or introduces breaking logic.
    - Ensure the build system verifies the manifest integrity.

---

## Summary of Handshake Workflow

1.  **Connection Initiation**: Android App establishes physical link.
2.  **Discovery**: Android App sends `GET_VERSION` (0x06).
3.  **Reporting**: Firmware responds with its version (e.g., `0.2.0`).
4.  **Enforcement**: Android App validates the version against the whitelist (Phase 1) or manifest (Phase 2).
5.  **Outcome**:
    - **Success**: Proceed to standard operation.
    - **Failure**: Transport is closed; user is notified of the required update.
