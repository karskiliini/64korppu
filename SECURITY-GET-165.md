# GET-165 security correction

Malformed FAT12 cluster numbers could index beyond the cached FAT (Pico) or outside its SRAM region (Nano). Reject them before memory/sector access. The real parser is tested with adversarial cluster 65535, rejected writes, and an invalid file chain. Normal existing tests remain enabled.

Pico also truncated the 32-bit remaining file size at 64 KiB; this is now compared without truncation. The new real-parser test exposed two existing compile blockers (missing eof field and invalid hexadecimal serial literal), repaired to run that parser at all.

Verification: focused AddressSanitizer/UndefinedBehaviorSanitizer checks and the existing native make test command. No firmware was flashed and no physical drive was accessed. Device compiler, electrical behavior and hardware integration remain outside these host checks.
