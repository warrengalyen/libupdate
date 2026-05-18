# libupdate

A cross-platform C library for secure, self-updating desktop applications. It provides a minimal API to check for updates from a JSON manifest, download verified ZIP packages, and safely apply them using a separate **updater** executable while the main application exits. Designed for reuse across projects, it requires no configuration files. Ships as a shared library (`update.dll` / `libupdate.so`) and a standalone `updater` / `updater.exe` that performs file replacement and relaunches the app.

## How it works

1. **`update_check`** reads the manifest; **`update_download`** (or **`update_perform`**) fetches the ZIP.
2. **`update_apply`** spawns **`updater`** next to your executable (package path, install dir, parent PID, app name), then **exits** on success so the updater can run.
3. The updater waits for the app to exit, extracts to **`*.update_staging`**, keeps a copy of the old tree under **`*.update_backup`** (rename when possible; otherwise a **full-tree copy** when the install root cannot be renamed), overlays files from staging onto the install, swaps **`updater_new` -> `updater`**, then cleans up and relaunches the app.

## Manifest format

The remote URL should serve a JSON object:

```json
{
  "version": "1.2.0",
  "download_url": "https://example.com/releases/app-1.2.0.zip",
  "checksum": "a3f1c6...64-char hex SHA-256...",
  "description_format": "html",
  "description": "<b>New Features</b><br><ul><li>Faster startup</li></ul>"
}
```

Required keys are `version`, `download_url`, and `checksum`.

Optional rich-text release notes:

| Key | Meaning |
|-----|---------|
| `description_format` | Omit or `plaintext` for plain text (HTML special characters are escaped). Use `html` for a **restricted** tag subset (`b`, `i`, `u`, `br`, `p`, `ul`, `ol`, `li`, `a`, `span`). Other markup and unsafe URLs/attributes are stripped. Unknown formats are treated as plaintext. |
| `description` | Release notes string (large payloads are capped; see implementation). |

The library **does not render** HTML; it parses the manifest, sanitizes or escapes `description`, and exposes it via `update_info_t`. Your UI renders the result with its native HTML or text widget.

`download_url` is capped by the `update_info_t` field size (512 bytes including the terminating NUL). If you fetch the manifest yourself, you can use **`update_manifest_parse`** on the JSON body and then **`update_info_free`** when done.

## Building

Requires CMake 3.16+ and a C11 compiler. Python 3 is needed to generate test fixtures.

```bash
git clone --recurse-submodules https://github.com/warrengalyen/libupdate
cd libupdate
cmake -B build
cmake --build build
```

To set the embedded version string the library compares against:

```bash
cmake -B build -DCMAKE_C_FLAGS="-DUPDATE_APP_VERSION_STRING=\\\"1.0.0\\\""
```

To skip building tests:

```bash
cmake -B build -DLIBUPDATE_BUILD_TESTS=OFF
```

Built artifacts are copied to `bin/`.

### Dependencies

- [miniz](https://github.com/richgel999/miniz) -- ZIP extraction (git submodule)
- [Unity](https://github.com/ThrowTheSwitch/Unity) -- test framework (submodule or auto-fetched)
- WinHTTP (Windows) / POSIX sockets + [OpenSSL](https://www.openssl.org/) (Linux/macOS) for HTTP/HTTPS

## Testing

```bash
cmake --build build --target check
```

Integration tests against a mock HTTP server require:

```bash
export LIBUPDATE_INTEGRATION_TESTS=1
cmake --build build --target check
```

On native Windows (WinHTTP), also set `LIBUPDATE_INTEGRATION_WINHTTP=1`.

## Usage

Paths: `update_init` rejects non-absolute `install_dir` and `temp_dir` when those options are set. `update_download` requires an **absolute** destination path. Use `update_path_make_absolute` when you only have a relative path (e.g. built from user input or `./update.zip`).
### One-shot update (check + download + apply)

```c
#include <update.h>

int main(void)
{
    update_options_t opts = {0};
    opts.update_url = "https://example.com/update.json";
    opts.app_name    = "myapp.exe"; /* argv[0]-style name passed to updater; can be relative to install_dir */
    opts.install_dir = "C:\\Program Files\\MyApp"; /* optional but recommended: absolute install root */
    opts.temp_dir    = "C:\\ProgramData\\MyApp\\tmp"; /* optional: absolute base for download workdir */

    if (update_init(&opts) != UPDATE_OK)
        return 1;

    int status = update_perform();

    if (status == UPDATE_STARTED) {
        /* updater is running; exit so it can replace files and relaunch this app */
        return 0;
    }

    if (status == UPDATE_NOOP) {
        /* already up to date */
    }

    update_shutdown();
    return 0;
}
```

If `install_dir` is omitted, the library defaults the install target to the **directory containing the running application executable** (so updates still work when the process current directory is unrelated to the install).
### Step-by-step update

```c
#include <update.h>
#include <stdio.h>

int main(void)
{
    char zip_abs[4096];
    update_options_t opts = {0};
    opts.update_url = "https://example.com/update.json";
    opts.app_name   = "myapp";
    opts.install_dir = "/opt/myapp"; /* optional; must be absolute if set */

    if (update_init(&opts) != UPDATE_OK)
        return 1;

    update_info_t info;
    int st = update_check(&info);

    if (st == UPDATE_AVAILABLE) {
        printf("New version: %s\n", info.version);

        /* update_download requires an absolute dest path */
        if (update_path_make_absolute("/tmp/update.zip", zip_abs, sizeof zip_abs) != UPDATE_OK)
            goto out;

        if (update_download(info.download_url, zip_abs) != UPDATE_OK)
            goto out;

        /* Manifest checksum is not applied automatically in this path unless you set
           opts.expected_sha256 at init; verify explicitly before apply: */
        if (update_verify(zip_abs, info.checksum) != UPDATE_OK)
            goto out;

        /* update_apply absolutizes internally but pass a path you already validated */
        update_apply(zip_abs);
        /* does not return on success */
    }

out:
    update_info_free(&info);
    update_shutdown();
    return 0;
}
```

On Windows, use absolute paths such as `C:\\Users\\Public\\myapp-update.zip` (or run them through `update_path_make_absolute` first).

### Optional: pin download digest via `update_init`

If you already know the expected SHA-256 before downloading (e.g. second channel or cached manifest), set `opts.expected_sha256` in `update_init`; `update_download` will then verify the file and delete it on mismatch.

### Progress callback

```c
void on_progress(unsigned long long done, unsigned long long total, void *user)
{
    if (total > 0)
        printf("\r%llu / %llu bytes", done, total);
}

/* after update_init */
update_set_download_progress_callback(on_progress, NULL);
```

## Platform notes

- **Windows**: Uses WinHTTP (HTTPS natively). If the updater lives under the install directory, install-root rename may fail; the built-in **mirror + in-place overlay** path handles that so updates still complete and the app relaunches.
- **Linux/macOS**: Uses POSIX sockets with OpenSSL for HTTPS. If OpenSSL is not found at build time, only plain HTTP is available (a CMake warning is emitted).
- HTTP redirects are not followed by either backend.

## License

MIT
