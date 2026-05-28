; =============================================================================
; syscalls.inc — INT 0x80 syscall function numbers
;
; These constants define the function numbers passed in AH before executing
; INT 0x80.  The kernel dispatches based on AH and performs the actual
; BIOS/hardware access on behalf of user-mode code.
;
; Convention (16-bit real mode):
;   AH = function number (from this file)
;   Other registers = function-specific arguments (see doc/SYSTEM-CALLS.md)
;   Return: function-specific (typically AX = result, CF = error)
;
; IMPORTANT: These numbers are a STABLE ABI.  Once assigned, a number never
; changes.  New syscalls are appended at the end.  See SYSTEM-CALLS.md §7.3.
; =============================================================================

SYS_PRINT_STRING  equ 0x01          ; DS:SI = NUL-terminated string
SYS_PRINT_CHAR    equ 0x02          ; AL = character to print
SYS_READ_KEY      equ 0x03          ; Returns: AH=scancode, AL=ASCII
SYS_READ_SECTOR   equ 0x04          ; EDI=LBA, ES:BX=buffer, CL=count
SYS_GET_VERSION   equ 0x05          ; Returns: AH=major, AL=minor
SYS_CLEAR_SCREEN  equ 0x06          ; No arguments
SYS_SET_CURSOR    equ 0x07          ; DH=row, DL=column
SYS_GET_CURSOR    equ 0x08          ; Returns: DH=row, DL=col
SYS_CHECK_A20     equ 0x09          ; Returns: AL=1 if enabled, 0 if not
SYS_GET_CONV_MEM  equ 0x0A          ; Returns: AX=KB of conventional memory
SYS_GET_EXT_MEM   equ 0x0B          ; Returns: AX=KB of extended, CF=error
SYS_GET_E820      equ 0x0C          ; EBX=continuation, ES:DI=buf
SYS_REBOOT        equ 0x0D          ; Warm reboot (does not return)
SYS_GET_DRIVE_INFO equ 0x0E         ; Returns: drive geometry, CF=error
SYS_GET_BIB       equ 0x0F          ; Returns: ES:BX = BIB address
SYS_PRINT_HEX8    equ 0x10          ; AL = byte to print as hex
SYS_PRINT_HEX16   equ 0x11          ; DX = word to print as hex
SYS_PRINT_DEC16   equ 0x12          ; DX = word to print as decimal
SYS_WAIT_KEY      equ 0x13          ; Print prompt, wait, clear screen
SYS_GET_EQUIP     equ 0x14          ; Returns: AX = equipment word
SYS_GET_VIDEO     equ 0x15          ; Returns: AL=mode, AH=cols, BH=page
SYS_GET_BDA_BYTE  equ 0x16          ; BX=BDA offset; Returns: AL=byte
SYS_GET_BDA_WORD  equ 0x17          ; BX=BDA offset; Returns: AX=word
SYS_CPUID         equ 0x18          ; EDI=leaf; Returns: EAX,EBX,ECX,EDX
SYS_CHECK_CPUID   equ 0x19          ; Returns: AL=1 if supported
SYS_GET_EDD       equ 0x1A          ; DL=drive; Returns: EDD info, CF=err
SYS_GET_IVT       equ 0x1B          ; CL=vector#; Returns: AX=off, DX=seg

; --- Debug syscalls (0x20–0x22) -----------------------------------------------
; These are kernel-mediated debug output.  User-mode programs call these instead
; of touching COM1 directly.  In release builds, the handlers are no-ops.
;
; All debug syscalls accept an optional caller tag:
;   DS:BX = NUL-terminated tag string (e.g., "SHL", "FS")
;           If BX=0, the kernel uses a default tag "USR".
;
SYS_DBG_PRINT     equ 0x20          ; DS:SI = message, DS:BX = tag (or 0)
SYS_DBG_HEX16     equ 0x21          ; DX = value, DS:BX = tag (or 0)
SYS_DBG_REGS      equ 0x22          ; Dump AX,BX,CX,DX,SI,DI; DS:BX = tag

; --- Program loader syscalls (0x23–0x24) --------------------------------------
; These support user programs loaded by the shell's `run` command.
;
; SYS_EXIT: Terminates the running program and returns to the shell, regardless
;   of call depth.  Restores SP from SHELL_SAVED_SP and does a near return.
;
; SYS_GET_ARGS: Returns a pointer to the command-line arguments passed to the
;   program (everything after the filename in `run PROG.MNX args...`).
;
SYS_EXIT          equ 0x23          ; AL = exit code (reserved); does not return
SYS_GET_ARGS      equ 0x24          ; Returns: SI = pointer to args, CX = length
SYS_GET_ARGC      equ 0x25          ; Returns: CL = argument count
SYS_GET_ARGV      equ 0x26          ; CL = index; Returns: SI = arg, CX = length
SYS_EXEC          equ 0x27          ; DS:SI = 11-byte filename, DS:DI = args (or 0)
                                    ; Does not return on success; CF set on failure
SYS_SPAWN         equ 0x28          ; Like SYS_EXEC but reloads caller after child exits
                                    ; DS:SI = child filename, DS:DI = child args,
                                    ; DS:BX = caller's 11-byte filename

SYSCALL_MAX       equ 0x28          ; Highest valid syscall number
