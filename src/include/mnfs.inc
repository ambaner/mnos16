; =============================================================================
; mnfs.inc — MNFS (MNOS16 Flat Filesystem) constants
;
; Defines the on-disk layout of the MNFS directory table, directory entry
; structure, file attributes, and INT 0x81 filesystem syscall numbers.
;
; MNFS is a flat (no subdirectories) filesystem with a 1-sector directory
; table at a fixed partition offset.  Files are stored as contiguous sectors.
; See doc/FILESYSTEM.md for the complete specification.
;
; Used by:
;   - VBR, LOADER, KERNEL (via find_file.inc for boot-time directory lookup)
;   - FS.SYS (filesystem module, implements INT 0x81 handler)
;   - SHELL (dir command, uses INT 0x81 via FS_* constants)
; =============================================================================

; --- Disk layout -------------------------------------------------------------
MNFS_DIR_SECTOR   equ 2              ; Directory table at partition sector 2
MNFS_DIR_SECTORS  equ 1              ; Directory is 1 sector (512 bytes)

; --- Directory header (first 32 bytes of directory sector) -------------------
MNFS_MAGIC        equ 0x53464E4D     ; 'MNFS' as little-endian dword
MNFS_HDR_SIZE     equ 32             ; Header size in bytes
MNFS_HDR_MAGIC    equ 0              ; Offset: 4-byte magic ('MNFS')
MNFS_HDR_VERSION  equ 4              ; Offset: 1-byte version (0x01)
MNFS_HDR_COUNT    equ 5              ; Offset: 1-byte file count
MNFS_HDR_TOTAL    equ 6              ; Offset: 2-byte total sectors used
MNFS_HDR_CAPACITY equ 8              ; Offset: 2-byte total data area sectors

; --- Directory entry (32 bytes each, starting at offset 32) ------------------
MNFS_ENTRY_SIZE   equ 32             ; Bytes per directory entry
MNFS_MAX_ENTRIES  equ 15             ; (512 - 32) / 32 = 15 entries max
MNFS_NAME_LEN     equ 11             ; 8 filename + 3 extension (8.3 format)

; Entry field offsets (relative to start of entry)
MNFS_ENT_NAME     equ 0              ; 11 bytes: filename (8) + extension (3)
MNFS_ENT_ATTR     equ 11             ; 1 byte:  attribute flags
MNFS_ENT_START    equ 12             ; 4 bytes: start sector (partition-relative)
MNFS_ENT_SECTORS  equ 16             ; 2 bytes: file size in sectors
MNFS_ENT_BYTES    equ 18             ; 4 bytes: actual file size in bytes

; --- Attribute flags ---------------------------------------------------------
MNFS_ATTR_SYSTEM  equ 0x01           ; System file (LOADER, KERNEL, FS)
MNFS_ATTR_EXEC    equ 0x02           ; User-mode executable (MNEX format)

; --- MNEX binary header (first 6 bytes of every loadable binary) -------------
; Offset 0: 4-byte magic signature ('MNLD', 'MNKN', 'MNFS', 'MNEX')
; Offset 4: 2-byte sector count
; Code entry point is always at offset 6, right after the header.
MNEX_HDR_SIZE     equ 6              ; Header size — callers must jump past it

; --- MNEX v2 header (relocatable modules and programs) ------------------------
; Used by FS.SYS, MM.SYS, SHELL.SYS, and .MNX user programs.
; The loader/shell detects v2 by checking the flags field at offset 6.
;
; Offset 0:  db magic[4]      ; Module magic ('MNFS', 'MNMM', 'MNEX')
; Offset 4:  dw sector_count  ; Total module size in 512-byte sectors
; Offset 6:  dw flags         ; Bit 0: has relocation table
; Offset 8:  dw reloc_count   ; Number of 16-bit relocation entries
; Offset 10: dw entry_offset  ; Offset from load base to first instruction
; Offset 12: dw reloc_table[] ; reloc_count x 2-byte offsets into module
;            ...code/data...  ; Code begins at entry_offset
;
; The kernel applies relocations to system modules, and the shell applies
; them to user programs, by adding the load base address to the 16-bit word
; at each offset in the relocation table.
;
; Legacy v1 programs (6-byte header, no relocs) are still supported by the
; shell — detected when EITHER flags bit 0 is clear OR the secondary
; validation fails (entry_offset != 12 + reloc_count * 2).  The secondary
; check prevents false-positive v2 detection when a v1 binary's first
; opcode happens to have bit 0 set (e.g., 0xEB jmp, 0x55 push bp, 0xC3 ret).
MNEX_V2_HDR_BASE  equ 12             ; Fixed header fields before reloc table
MNEX_V2_FLAG_RELOC equ 0x0001        ; flags bit 0: relocation table present


; These are function numbers passed in AH before INT 0x81.
; FS.SYS dispatches based on AH.  See doc/FILESYSTEM.md §8.
FS_LIST_FILES     equ 0x01           ; ES:BX=buf → CL=count, buf=directory
FS_FIND_FILE      equ 0x02           ; DS:SI=name → EAX=start, CX=sectors
FS_READ_FILE      equ 0x03           ; DS:SI=name, ES:BX=buf, CX=max → CX=read
FS_GET_INFO       equ 0x04           ; → AL=ver, CL=count, CH=max, DX=total
FS_FIND_BASE      equ 0x05           ; DS:SI=8-byte name → EAX=start, CX=sectors
FS_WRITE_FILE     equ 0x06           ; DS:SI=name, ES:BX=buf, ECX=bytes, DL=attr
FS_DELETE_FILE    equ 0x07           ; DS:SI=name → CF=error
FS_RENAME_FILE    equ 0x08           ; DS:SI=old name, ES:DI=new name

FS_SYSCALL_MAX    equ 0x08           ; Highest valid FS syscall number

; --- Filesystem error codes (returned in AL when CF is set) ------------------
FS_ERR_NOT_FOUND  equ 0x01           ; File not found
FS_ERR_EXISTS     equ 0x02           ; File already exists (write/rename)
FS_ERR_DIR_FULL   equ 0x03           ; Directory full (15 entries)
FS_ERR_DISK_FULL  equ 0x04           ; Insufficient disk space
FS_ERR_IO         equ 0x05           ; Disk I/O error
FS_ERR_PROTECTED  equ 0x06           ; Cannot modify system file

; --- Deleted entry marker ----------------------------------------------------
MNFS_DELETED      equ 0xE5           ; First byte of name = entry deleted

; --- Write operation constants -----------------------------------------------
MNFS_WRITE_CHUNK  equ 16             ; Max sectors per INT 0x13 write (DMA safe)
