# NKU OS Lab

## 坑

1. 最新版本的 QEMU 使用的是 dynamic 版本的 OpenSBI，这会导致内核无法被加载到 `0x80200000` 这个地址，需要指定 OpenSBI 为 jump 版本。jump 版本的 OpenSBI 二进制文件位于 `./bootloader/opensbi.bin`

## Reports

1. [Lab0.5 & Lab1](./docs/report-lab1.md)
2. [Lab2](./docs/report-lab2.md)
3. [Lab3](./docs/report-lab3.md)
4. [Lab4](./docs/report-lab4.md)
