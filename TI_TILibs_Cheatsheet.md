# TI tilibs Cheat Sheet

Quick reference for using `libticables`, `libticalcs`, `libtifiles`, and `libticonv` to communicate with TI-83 Plus calculators over a SilverLink cable.

---

## Library Lifecycle
- Call these once before any other API usage:
  - `ticables_library_init()`
  - `tifiles_library_init()`
  - `ticalcs_library_init()`
- Shut down in reverse order when finished: `ticalcs_library_exit()`, `tifiles_library_exit()`, `ticables_library_exit()`
- `ticables_version_get()`, `tifiles_version_get()`, `ticalcs_version_get()` return runtime library versions.

## Cable (SilverLink) Setup
- Create and configure a cable handle:
  - `ticables_handle_new(CABLE_SLV, PORT_x)` – choose the SilverLink model and appropriate port.
  - Optional tweaks:
    - `ticables_options_set_timeout(handle, timeout_dectenths)`
    - `ticables_options_set_delay(handle, delay_us)`
- Open/close and maintain the connection:
  - `ticables_cable_open(handle)` / `ticables_cable_close(handle)`
  - `ticables_cable_reset(handle)` to clear line state.
  - `ticables_cable_probe(handle, &result)` checks for a connected calculator.
- Direct byte IO and status (rarely needed when using libticalcs high-level calls):
  - `ticables_cable_send()`, `ticables_cable_recv()`
  - `ticables_cable_check(handle, &status)`
  - `ticables_cable_put()` / `ticables_cable_get()`
- Event hooks & inspection:
  - `ticables_cable_set_event_hook(handle, hook)`
  - `ticables_cable_set_event_user_pointer(handle, ctx)`
  - `ticables_cable_get_event_count(handle)`
  - `ticables_handle_show(handle)` dumps configuration info (stdout).

## Calculator Handle & Attachment
- Create a calculator session for a TI‑83 Plus:
  - `CalcHandle *calc = ticalcs_handle_new(CALC_TI83P);`
- Attach the cable:
  - `ticalcs_cable_attach(calc, cable_handle);` / `ticalcs_cable_detach(calc);`
- Capability discovery:
  - `ticalcs_calc_features(calc)` returns an `OPS_*/FTS_*` bitmask (e.g., `OPS_KEYS`, `OPS_VARS`, `OPS_FLASH`).
  - `ticalcs_model_supports_dbus/dusb/nsp/installing_flashapps()` for protocol-specific checks.
- Readiness & info:
  - `ticalcs_calc_isready(calc)` waits for handshake.
  - `ticalcs_calc_get_version(calc, &CalcInfos)` fills OS/boot, RAM/Flash sizes, etc.
  - `ticalcs_calc_get_memfree(calc, &ram, &flash)`
  - `ticalcs_calc_get_dirlist(calc, &vars_tree, &apps_tree)` plus helpers in `dirlist.c`.

## Screen & Key Interaction
- Screenshots:
  - `ticalcs_calc_recv_screen(calc, &coord, &bitmap)` returns raw calc-format display.
  - `ticalcs_calc_recv_screen_rgb888()` gives RGB565/monochrome converted to RGB888.
  - Convert or postprocess with `ticalcs_screen_convert_*` utilities.
  - Free buffers with `ticalcs_free_screen(bitmap)`.
- Remote keypress automation:
  - `ticalcs_calc_send_key(calc, scancode)`
  - Lookup scancodes via `ticalcs_keys_83p(uint8_t ascii)` or header `keys83p.h`.

## Program & Data Transfer
- Regular variables:
  - Build `FileContent` via `tifiles_content_create_regular(CALC_TI83P)` and fill `VarEntry` records.
  - Send: `ticalcs_calc_send_var(calc, MODE_NORMAL, file_content);`
  - Receive: `ticalcs_calc_recv_var(calc, MODE_NORMAL, file_content, &request);`
  - Non-silent transfers: `*_var_ns` variants.
- Applications / OS / Certificates:
  - `ticalcs_calc_send_app()`, `ticalcs_calc_send_os()`, `ticalcs_calc_send_cert()` and matching `recv_*`.
  - Use `tifiles_content_create_flash()` or `tifiles_content_create_backup()` for structured loads.
- Backups & TIGroups:
  - `ticalcs_calc_send_backup()` / `recv_backup()`
  - `ticalcs_calc_send_all_vars_backup()` / `recv_all_vars_backup()` (fake-backup bundles).
  - `ticalcs_calc_send_tigroup()` / `recv_tigroup()` for combined content packages.
- Variable management:
  - `ticalcs_calc_del_var(calc, &VarRequest)`
  - `ticalcs_calc_new_fld(calc, &VarRequest)` (folders)
  - `ticalcs_calc_rename_var()` / `ticalcs_calc_change_attr()`

## File & Variable Utilities (`libtifiles`)
- File type helpers: `tifiles_file_is_*`, `tifiles_file_get_model`, `tifiles_fext_of_*`
- Create/read/write content:
  - `tifiles_file_read_regular(path, content)`
  - `tifiles_file_write_regular(path, content, &out)` (similar APIs for backup/flash/group).
- Manage entries:
  - `tifiles_ve_create[_alloc_data|_with_data2]()`
  - `tifiles_ve_copy`, `tifiles_ve_delete`, `tifiles_ve_create_array`
  - Flash pages: `tifiles_fp_*` suite
- Checksums & metadata: `tifiles_checksum()`, `tifiles_vartype2string()`, etc.

## Character & Token Conversion (`libticonv`)
- Convert between UTF-16 and TI-83 Plus charset:
  - `ticonv_utf16_to_ti83p(const uint16_t *utf16, char *ti)`
  - `ticonv_ti83p_to_utf16(const char *ti, uint16_t *utf16)`
- General token utilities in `ticonv.cc` and `tokens.cc` handle parsing TI token strings for various models.

## Probing & Detection
- Without a pre-known model:
  - `ticalcs_probe(CABLE_SLV, PORT_x, &CalcModel, all_devices)`
  - `ticalcs_probe_calc(cable_handle, &model)` for already-open cable handles.
- USB devices (for future expansion):
  - `ticables_get_usb_device_info()` -> `ticalcs_device_info_to_model()`.

## Events & Progress
- Register callbacks to monitor transfer progress or log packets:
  - Calculator: `ticalcs_calc_set_event_hook(calc, hook)`, `ticalcs_calc_set_event_user_pointer(calc, ctx)`
  - Cable: `ticables_cable_set_event_hook(handle, hook)`
- Within hooks, inspect `CalcEventData` or `CableEventData` to trace DBUS/DUSB frames and state changes.

## Error Handling
- Most functions return `ERR_*` codes (zero/non-zero).
- Translate to human-readable strings:
  - `ticalcs_error_get(err, &msg)` / `ticalcs_error_free(msg)`
  - `ticables_error_get()` / `ticables_error_free()`
  - `tifiles_error_get()` / `tifiles_error_free()`
- Common statuses: `ERR_NO_CABLE`, `ERR_BUSY`, `ERR_INVALID_HANDLE`, `ERROR_READ_TIMEOUT`, etc.

## Typical Session Skeleton
```c
ticables_library_init();
tifiles_library_init();
ticalcs_library_init();

CableHandle *cable = ticables_handle_new(CABLE_SLV, PORT_1);
ticables_cable_open(cable);

CalcHandle *calc = ticalcs_handle_new(CALC_TI83P);
ticalcs_cable_attach(calc, cable);
ticalcs_calc_isready(calc);
// ... transfer vars, send keys, etc. ...

ticalcs_cable_detach(calc);
ticalcs_handle_del(calc);
ticables_cable_close(cable);
ticables_handle_del(cable);

ticalcs_library_exit();
tifiles_library_exit();
ticables_library_exit();
```

---

Keep this file alongside `main.c` so you can quickly recall the essential APIs while integrating TI calculator communication into your project.
