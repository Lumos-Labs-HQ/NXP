#!/bin/bash
export PATH="/usr/local/go/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export CGO_ENABLED=1
cd /mnt/d/code/Experment/NXP/tests/go
exec go test -v -count=1 .
