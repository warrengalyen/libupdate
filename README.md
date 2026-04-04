# libupdate

An application self-update library. Ships as a shared library (`update.dll` / `libupdate.so`) and a standalone `updater` executable that performs the file replacement while the main application is not running.

## How it works

1. Your app calls `update_check` to fetch a JSON manifest from a remote URL.
2. If a newer version is available, download the ZIP package with `update_download`.
3. Call `update_apply` (or the all-in-one `update_perform`) to spawn the `updater` process, which waits for your app to exit, extracts the ZIP over the install directory, and relaunches the app.

The updater uses atomic rename + backup so a failed install can be rolled back.

## Manifest format

The remote URL should serve a JSON object:

```json
{
  "version": "1.2.0",
  "download_url": "https://example.com/releases/app-1.2.0.zip",
  "checksum": "a3f1c6...64-char hex SHA-256..."
}
```

## Building

Requires CMake 3.16+ and a C11 compiler. Python 3 is needed to generate test fixtures.

```bash
git clone --recurse-submodules <repo-url>
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

### One-shot update (check + download + apply)

```c
#include <update.h>

int main(void)
{
    update_options_t opts = {0};
    opts.update_url = "https://example.com/update.json";
    opts.app_name   = "myapp";

    if (update_init(&opts) != UPDATE_OK)
        return 1;

    int status = update_perform();

    if (status == UPDATE_STARTED) {
        /* updater is running; exit so it can replace files */
        return 0;
    }

    if (status == UPDATE_NOOP) {
        /* already up to date */
    }

    update_shutdown();
    return 0;
}
```

### Step-by-step update

```c
#include <update.h>

int main(void)
{
    update_options_t opts = {0};
    opts.update_url = "https://example.com/update.json";
    opts.app_name   = "myapp";

    if (update_init(&opts) != UPDATE_OK)
        return 1;

    update_info_t info;
    int st = update_check(&info);

    if (st == UPDATE_AVAILABLE) {
        printf("New version: %s\n", info.version);

        if (update_download(info.download_url, "/tmp/update.zip") == UPDATE_OK) {
            update_apply("/tmp/update.zip");
            /* does not return on success */
        }
    }

    update_shutdown();
    return 0;
}
```

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

- **Windows**: Uses WinHTTP (supports HTTPS natively).
- **Linux/macOS**: Uses POSIX sockets with OpenSSL for HTTPS. If OpenSSL is not found at build time, only plain HTTP is available (a CMake warning is emitted).
- HTTP redirects are not followed by either backend.

## License

MIT
