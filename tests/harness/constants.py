"""Auto-generated from src/include/*.inc — DO NOT EDIT MANUALLY.

Regenerate with:  python tests/gen_constants.py
"""


# --- From memory.inc --------------------------------------------------
LOADER_SEG           = 0x00
LOADER_OFF           = 0x0800
KERNEL_SEG           = 0x00
KERNEL_OFF           = 0x5000
MODULE_FIRST_BASE    = 0x0800
MODULE_AREA_END      = 0x5000
DIR_SCRATCH_BUF      = 0x4E00
STACK_CANARY_ADDR    = 0x6C00
STACK_CANARY_VALUE   = 0xDEAD
STACK_CANARY_SIZE    = 4
HMA_SEG              = 0xFFFF
HMA_HEAP_START       = 0x10
HMA_HEAP_END         = 0xFF00
HMA_HEAP_SIZE        = HMA_HEAP_END - HMA_HEAP_START
HEAP_START           = 0x8000
HEAP_END             = 0x9000
HEAP_SIZE            = HEAP_END - HEAP_START
USER_PROG_BASE       = 0x8000
USER_PROG_END        = 0xF7FF
USER_PROG_MAX        = 0x7800
USER_PROG_MAX_SEC    = 60
SHELL_SAVED_SP       = 0x7FFE
SHELL_ARGS_PTR       = 0x7FFC
ARGV_TABLE           = 0x7F00
ARGV_ARGC            = 0x7F00
ARGV_PTRS            = 0x7F02
ARGV_STORAGE         = 0x7F22
ARGV_STORAGE_END     = 0x7FFB
ARGV_MAX_ARGS        = 15
MCB_SIZE_OFF         = 0
MCB_FLAGS_OFF        = 2
MCB_MAGIC_OFF        = 3
MCB_HDR_SIZE         = 4
MCB_MAGIC            = 0x4D
MCB_FLAG_USED        = 0x01
MCB_OWNER_MASK       = 0x0E
MCB_OWNER_SHIFT      = 1
MCB_MIN_BLOCK        = 8
MCB_OWNER_NONE       = 0
MCB_OWNER_KERN       = 1
MCB_OWNER_FS         = 2
MCB_OWNER_MM         = 3
MCB_OWNER_SHELL      = 4
MCB_OWNER_USR1       = 5
MCB_OWNER_USR2       = 6
MCB_OWNER_USR3       = 7
MEM_ALLOC            = 0x01
MEM_FREE             = 0x02
MEM_AVAIL            = 0x03
MEM_INFO             = 0x04
MEM_QUERY            = 0x05
MEM_SYSCALL_MAX      = 0x05

# --- From syscalls.inc ------------------------------------------------
SYS_PRINT_STRING     = 0x01
SYS_PRINT_CHAR       = 0x02
SYS_READ_KEY         = 0x03
SYS_READ_SECTOR      = 0x04
SYS_GET_VERSION      = 0x05
SYS_CLEAR_SCREEN     = 0x06
SYS_SET_CURSOR       = 0x07
SYS_GET_CURSOR       = 0x08
SYS_CHECK_A20        = 0x09
SYS_GET_CONV_MEM     = 0x0A
SYS_GET_EXT_MEM      = 0x0B
SYS_GET_E820         = 0x0C
SYS_REBOOT           = 0x0D
SYS_GET_DRIVE_INFO   = 0x0E
SYS_GET_BIB          = 0x0F
SYS_PRINT_HEX8       = 0x10
SYS_PRINT_HEX16      = 0x11
SYS_PRINT_DEC16      = 0x12
SYS_WAIT_KEY         = 0x13
SYS_GET_EQUIP        = 0x14
SYS_GET_VIDEO        = 0x15
SYS_GET_BDA_BYTE     = 0x16
SYS_GET_BDA_WORD     = 0x17
SYS_CPUID            = 0x18
SYS_CHECK_CPUID      = 0x19
SYS_GET_EDD          = 0x1A
SYS_GET_IVT          = 0x1B
SYS_DBG_PRINT        = 0x20
SYS_DBG_HEX16        = 0x21
SYS_DBG_REGS         = 0x22
SYS_EXIT             = 0x23
SYS_GET_ARGS         = 0x24
SYS_GET_ARGC         = 0x25
SYS_GET_ARGV         = 0x26
SYSCALL_MAX          = 0x26

# --- From mnfs.inc ----------------------------------------------------
MNFS_DIR_SECTOR      = 2
MNFS_DIR_SECTORS     = 1
MNFS_MAGIC           = 0x53464E4D
MNFS_HDR_SIZE        = 32
MNFS_HDR_MAGIC       = 0
MNFS_HDR_VERSION     = 4
MNFS_HDR_COUNT       = 5
MNFS_HDR_TOTAL       = 6
MNFS_HDR_CAPACITY    = 8
MNFS_ENTRY_SIZE      = 32
MNFS_MAX_ENTRIES     = 15
MNFS_NAME_LEN        = 11
MNFS_ENT_NAME        = 0
MNFS_ENT_ATTR        = 11
MNFS_ENT_START       = 12
MNFS_ENT_SECTORS     = 16
MNFS_ENT_BYTES       = 18
MNFS_ATTR_SYSTEM     = 0x01
MNFS_ATTR_EXEC       = 0x02
MNEX_HDR_SIZE        = 6
MNEX_V2_HDR_BASE     = 12
MNEX_V2_FLAG_RELOC   = 0x01
FS_LIST_FILES        = 0x01
FS_FIND_FILE         = 0x02
FS_READ_FILE         = 0x03
FS_GET_INFO          = 0x04
FS_FIND_BASE         = 0x05
FS_WRITE_FILE        = 0x06
FS_DELETE_FILE       = 0x07
FS_RENAME_FILE       = 0x08
FS_SYSCALL_MAX       = 0x08
FS_ERR_NOT_FOUND     = 0x01
FS_ERR_EXISTS        = 0x02
FS_ERR_DIR_FULL      = 0x03
FS_ERR_DISK_FULL     = 0x04
FS_ERR_IO            = 0x05
FS_ERR_PROTECTED     = 0x06
MNFS_DELETED         = 0xE5
MNFS_WRITE_CHUNK     = 16

# --- MM stub entry points (offsets from CODE_BASE) ----------------------------
MM_ALLOC_ENTRY   = 0x00
MM_FREE_ENTRY    = 0x10
MM_AVAIL_ENTRY   = 0x20
MM_INFO_ENTRY    = 0x30
MM_INIT_ENTRY    = 0x40

# --- Test harness defaults ----------------------------------------------------
CODE_BASE        = 0x1000   # Where stub binaries are loaded
STACK_TOP        = 0xFFF0   # Initial SP
STRING_AREA      = 0x5000   # Where test input strings are placed

