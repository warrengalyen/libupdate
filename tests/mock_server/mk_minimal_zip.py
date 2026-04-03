"""Write a minimal ZIP (STORE only) compatible with miniz extraction tests."""
import sys
import zipfile


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: mk_minimal_zip.py <output.zip>", file=sys.stderr)
        return 2
    out = sys.argv[1]
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_STORED) as zf:
        zf.writestr("hello.txt", b"hello\n")
        zf.writestr("nested/a.txt", b"nested\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
