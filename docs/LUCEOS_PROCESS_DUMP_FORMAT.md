# LuceOS Process Dump Format v1

`procdump.exe --pid PID --mini PATH` and `--full PATH` write a streaming LuceOS
diagnostic file. This is **not** a Windows minidump. All integers are little
endian and records are emitted in order; consumers must stop at an incomplete
record or EOF.

`procdump.exe --top --output C:\\MicaNT\\processes.txt [--interval 2] [--count 5]`
captures one process table per sample (one second by default; without `--count`
it continues until the process is terminated),
prints each table to the console, and writes the same tables to the output
file. Use a new output path for each run because the current filesystem profile
does not support truncating an existing report. Rows are sorted by resident working set and include PID, image name,
thread and handle counts, resident working set, and private committed bytes.
The process snapshot contains active, non-terminating processes. Working set
is the kernel's per-process resident-page counter; private commit is virtual
commit and must not be interpreted as resident RAM. The current process
snapshot does not provide a reliable total virtual-size value, so the command
does not report one.

## Header

The 40-byte header is `LUCDMP1\0`, `uint32 version` (1), `uint32 headerSize`
(40), `uint16 architecture` (`0x8664`, AMD64), `uint16 flags` (bit 0 means
live best-effort capture; bit 1 means mini), `uint32 PID`, `uint64 kernelBuildId`
and `uint64 captureTime`. A zero build ID or time means that the kernel does not
currently expose that value.

Each following record starts with `uint32 type, uint32 payloadSize,
uint32 payloadCRC32`. The record checksum covers the complete payload. A zero
size has a zero checksum. Data payloads also include a CRC over the memory bytes
alone, allowing the memory chunk to be checked independently of its metadata.

| Type | Payload |
| --- | --- |
| 1 | Region: `uint64 base, uint64 size, uint32 state, uint32 protect, uint32 type, uint32 reserved` |
| 2 | Data: the record payload is `uint64 base, uint32 byteCount, uint32 dataCRC32`, then `byteCount` bytes. The record CRC covers the descriptor and the memory bytes together. |
| 3 | Unreadable: same 16-byte range descriptor as data; checksum is zero |
| 4 | Process: `uint32 PID, uint32 handleCount, uint32 threadCount, uint16 imageNameBytes, uint16 reserved`, then UTF-16 image name |
| 5 | Thread: the 80-byte native system thread record followed by `uint64 TEB, uint64 stackBase, uint64 stackLimit` (zero means unavailable) |
| 6 | Module: `uint64 imageBase, uint64 imageSize, uint16 pathBytes, uint16 reserved[3]`, then UTF-16 full path |
| 7 | Kernel crash context (248 bytes): `CRSH`, `uint16 version, uint16 size, uint32 stopCode, uint32 flags, uint64 parameters[4], uint64 buildId, uint32 CPU/PID/TID/stackBytes, uint64 RIP/RSP/RFLAGS/CR2, uint64 GPR[16], uint64 stackLimit/stackBase`. The header crash flag is bit 2 and its PID is zero. |
| 8 | Captured kernel stack: the type 2 data descriptor (`uint64 base, uint32 byteCount, uint32 dataCRC32`) followed by at most 512 stack bytes; base is the captured RSP. |
| `0xffffffff` | Successful end marker, zero payload |

For type 7, context flag bit 31 marks an explicitly synthetic fixture. Such a
record is useful for validating readers and CRC handling, but is not evidence
captured by a running kernel and says nothing about storage persistence.

CRC-32 uses the reflected IEEE polynomial `0xedb88320`, initial value
`0xffffffff`, and final complement. Data records are bounded to 64 KiB. A
process exit, changing mapping, inaccessible page or I/O failure can leave a
valid prefix without the end marker; absence of the end marker means partial
capture. Unreadable ranges are explicitly represented when the region walk
continues. Capture is live and best effort: threads are not suspended, so
metadata and bytes may describe different moments. Kernel virtual memory and
physical addresses are never included.

Mini mode includes module mappings, thread metadata and readable image, writable
private, and identified thread stack regions. Full mode includes all committed
readable user ranges. The system report intentionally returns JSON `null` for
PFN categories without canonical owner attribution; process working sets are
not summed to estimate global physical use.
