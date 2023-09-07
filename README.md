# NKU OS Lab

## 坑

1. 最新版本的 QEMU 使用的是 dynamic 版本的 OpenSBI，这会导致内核无法被加载到 `0x80200000` 这个地址，需要指定 OpenSBI 为 jump 版本。jump 版本的 OpenSBI 二进制文件位于 `./bootloader/opensbi.bin`
