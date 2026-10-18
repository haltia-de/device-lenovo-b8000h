# The build environment of the B8000-H image (ADR 0003): Alpine 3.24 x86_64
# with clang/LLVM for the armv7 kernel, mkbootimg, the tools the root file
# system build uses (apk for armv7, qemu-arm for its chroot steps,
# e2fsprogs) and debootstrap for the Debian island. The same packages as
# the b8000h-build:3.24 image used by hand.
FROM alpine:3.24
RUN apk add --no-cache \
	alpine-sdk android-tools-mkbootimg bash bc binutils bison busybox-static \
	ca-certificates clang cpio curl debootstrap debian-archive-keyring diffutils \
	dtc e2fsprogs e2fsprogs-extra elfutils-dev findutils flex gawk gcc git gmp-dev \
	grep gzip kmod linux-headers lld llvm llvm-dev make mpc1-dev mpfr-dev musl-dev \
	musl-utils ncurses-dev openssl-dev patch perl py3-pip python3 qemu-arm rsync \
	sed sfdisk tar util-linux wget xz zstd
RUN git config --global --add safe.directory '*'
