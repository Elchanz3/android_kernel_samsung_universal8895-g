#!/bin/bash



make O=out ARCH=arm64 exynos8895-greatlte_defconfig

make O=out -j8
