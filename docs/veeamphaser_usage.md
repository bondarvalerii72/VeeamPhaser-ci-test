# VeeamPhaser Usage Guide

Examples are shortened so they fit the page.
## `vbk`

The `vbk` command validates the backup header, scores each slot, and automatically chooses the best slot. It does not carve anything to disk, so you can immediately test or extract files as long as the VBK is readable. (encrypted vbk's will obviously fail)

```
VeeamPhaser.exe vbk tests\fixtures\hi_comp.vbk
```

```
00000000: <FileHeader version: d, inited: 1, digest_type_len: 3, digest_type: "md5", slot_fmt: 9, std_block_size: 100000, cluster_align: 3>

00001000: slot[0]: <CSlot crc=d1a313c0, has_snapshot=1, max_banks=7f00, allocated_banks=2 size=7f080>
bank 00: <BankInfo crc=639d3e6d, offset=      101000, size=  22000>
bank 01: <BankInfo crc=e5aeab25, offset=      123000, size=  22000>
00081000: slot[1]: <CSlot crc=c694b93b, has_snapshot=1, max_banks=7f00, allocated_banks=2 size=7f080>
bank 00: <BankInfo crc=639d3e6d, offset=      145000, size=  22000>
bank 01: <BankInfo crc=e5aeab25, offset=      167000, size=  22000>
using best slot @ 1000

processing tests\fixtures\hi_comp.vbk
0000:0003 Dir           3        6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)
0000:0005 IntFib        1 23Kb   6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/summary.xml
0000:000b IntFib       76 117Mb  6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/4a30b6bf-7c1e-4a8f-a45a-c7badd0302ff
0000:0011 IntFib        1 5Kb    6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/BackupComponents.xml
```

The header describes the VBK format, each slot entry shows bank CRCs and offsets, and at the end it shows the files contained in the chosen slot.

Commands to extract / test files in a vbk.

```
VeeamPhaser.exe vbk tests\fixtures\hi_comp.vbk --extract          # extract every file
VeeamPhaser.exe vbk tests\fixtures\hi_comp.vbk --extract 0000:0005 # extract the file with blockid 0000:0005
VeeamPhaser.exe vbk tests\fixtures\hi_comp.vbk --test # verify all files without writing them out
```

## `scan`

`scan` scans the VBK (or VIB) for slot headers and banks. With `--blocks` it also carves compressed data block and hashes them, the hashes are stored in an output CSV which can be loaded in an hashtable during extraction/testing, instead of relying on the BlockDescriptor vector.

```
VeeamPhaser.exe scan tests\fixtures\hi_comp.vbk --blocks


Found Slot at         1000,   7f080 bytes
Found Bank at       101000, crc 639d3e6d, size   22000 [bank  0 of slot 000000001000]
Found Bank at       123000, crc e5aeab25, size   22000 [bank  1 of slot 000000001000]
Found Slot at        81000,   7f080 bytes
Found Bank at       145000, crc 639d3e6d, size   22000 [bank  0 of slot 000000081000]
Found Bank at       167000, crc e5aeab25, size   22000 [bank  1 of slot 000000081000]
```

The scan logged and wrote each bank, complete slot, and the `tests\fixtures\hi_comp.vbk.out\carved_blocks.csv` so the hash table can be loaded during extraction/testing.

## `md`

The `md` command works with carved metadata (`.slot`) files, legacy_meta files, or (`.bank`) files but it has limited functionality with banks.

```
VeeamPhaser.exe md tests\fixtures\hi_comp.vbk.out\000000001000.slot
```

```
processing tests\fixtures\hi_comp.vbk.out\000000001000.slot

0000:0003 Dir           3        6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)
0000:0005 IntFib        1 23Kb   6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/summary.xml
0000:000b IntFib       76 117Mb  6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/4a30b6bf-7c1e-4a8f-a45a-c7badd0302ff
0000:0011 IntFib        1 5Kb    6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/BackupComponents.xml
```

md command without any options can be used to confirm which objects are stored in the slot before we start testing or carving data.

### Test files from metadata

Attach the VBK (`--vbk`) or the carved blocks csv and ask `md` to verify everything referenced in the metadata. Adding `--json-file` records the per-file results to disk.

```
VeeamPhaser.exe md tests\fixtures\hi_comp.vbk.out\000000001000.slot --vbk tests\fixtures\hi_comp.vbk --test --json-file tests\fixtures\hi_comp_test.json
```

The JSON output file contains one record per object:

```json
{"id":"0000:0005","pathname":"6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/summary.xml","size":23790,"type":"FT_INT_FIB","total_blocks":1,"sparse_blocks":0,"nOK":1,"percent":100.0,"nMissMD":0,"nMissHT":0,"nErrDecomp":0,"nErrCRC":0,"nReadErr":0,"md_fname":"tests\\fixtures\\hi_comp.vbk.out\\000000001000.slot"}
```

`type` identifies the object type (`FT_INT_FIB` == full backup file), while `total_blocks`, `sparse_blocks`, and `nOK` show how many blocks were referenced, empty and successfully processed. `nMissMD` counts descriptors missing from metadata, `nMissHT` counts blocks not found in the hash table, `nErrDecomp` counts decompression failures, `nErrCRC` counts checksum mismatches, and `nReadErr` counts raw read errors.

### Extract files from metadata

Provide the slot, name the file or it's block id you want to extract with `--extract` and specify the vbk file to get the data blocks.

```
VeeamPhaser.exe md tests\fixtures\hi_comp.vbk.out\000000001000.slot --vbk tests\fixtures\hi_comp.vbk --extract 0000:0005
```

```
[2025-11-05 07:16:33.832] [info] Extracting summary.xml = 1 blocks, 23Kb
[2025-11-05 07:16:33.833] [info] saved 23Kb to "tests\fixtures\hi_comp.vbk.out\6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)/summary.xml"
```

If you need to use the hashtable instead of the VBK file, simply add the `--device` and `--data` arguments. The `--device` argument should point to the file you carved or scanned, while `--data` should point to the resulting `carved_blocks.csv` file. This will load the hashtable and look up each hash within it.

You can also use `--no-vbk` when you only want to validate the metadata structure without touching the data, or `--skip-read` if you just want to confirm that blocks exist in the hashtable without decompressing them.


All the above information also works with "test".