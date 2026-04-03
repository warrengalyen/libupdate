"""Create a ZIP whose entry name uses parent segments (must be rejected by update_extract)."""
import sys
import zipfile

def main() -> int:
    if len(sys.argv) < 2:
        print("usage: mk_slip_zip.py <output.zip>", file=sys.stderr)
        return 2
    out = sys.argv[1]
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_STORED) as zf:
        zi = zipfile.ZipInfo("nested/../../evil_slip.txt")
        zf.writestr(zi, b"slip")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
