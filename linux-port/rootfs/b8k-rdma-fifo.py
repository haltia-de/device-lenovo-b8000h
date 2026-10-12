#!/usr/bin/python3
"""Re-apply the RDMA FIFO threshold that stops the Firefox scroll copies.

The kernel that contains this fix already writes 0x80f000e0. An older
image still writes the bootloader value 0x00f00010 on the first modeset.
That starts a line when only 256 bytes are buffered. 0x80f000e0 keeps the
same 3840-byte FIFO and waits until 3584 bytes are ready. Store that word
only when the register still holds 0x00f00010 and GLOBAL_CON is 0x101.
One ARM str; never a byte store. Any other value is left alone.
"""
import ctypes
import mmap
import sys
import time

GOOD = 0x00F00010
KEEP = 0x80F000E0
RDMA = 0x14006000
STUB = bytes([0x00, 0x10, 0x80, 0xE5, 0x4F, 0xF0, 0x7F, 0xF5, 0x1E, 0xFF, 0x2F, 0xE1])


def main():
    fd = open("/dev/mem", "r+b", buffering=0)
    mem = mmap.mmap(fd.fileno(), 0x1000, mmap.MAP_SHARED,
                    mmap.PROT_READ | mmap.PROT_WRITE, offset=RDMA)
    base = ctypes.addressof(ctypes.c_char.from_buffer(mem))
    code = mmap.mmap(-1, 4096, mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS,
                     mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
    code.write(STUB)
    caddr = ctypes.addressof(ctypes.c_char.from_buffer(code))
    libc = ctypes.CDLL(None, use_errno=True)
    syscall = libc.syscall
    syscall.restype = ctypes.c_long
    syscall.argtypes = [ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p,
                        ctypes.c_ulong]
    if syscall(0xF0002, ctypes.c_void_p(caddr), ctypes.c_void_p(caddr + 12), 0) != 0:
        print("FLUSH_FAIL")
        return 1
    store = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint32)(caddr)

    def rd(off):
        return ctypes.cast(base + off, ctypes.POINTER(ctypes.c_uint32)).contents.value

    passes = 1 if "--once" in sys.argv[1:] else 45
    wrote = 0
    for _ in range(passes):
        fifo = rd(0x40)
        gl = rd(0x10)
        if fifo == KEEP:
            pass
        elif fifo == GOOD and gl == 0x101:
            store(base + 0x40, KEEP)
            back = rd(0x40)
            if back != KEEP:
                store(base + 0x40, GOOD)
                print("REJECT %08x" % back)
                return 1
            wrote += 1
        time.sleep(1)
    print("FIFO %08x INT_EN %08x GLOBAL %08x wrote=%d" %
          (rd(0x40), rd(0), rd(0x10), wrote))
    return 0 if rd(0x40) == KEEP else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("EXC %s" % exc)
        sys.exit(1)
