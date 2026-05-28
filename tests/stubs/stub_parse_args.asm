; stub_parse_args.asm — Test harness for shell_parse_args
;
; Assembled as flat binary: nasm -f bin -I src/include/ -I src/shell/ -o tests/bin/stub_parse_args.bin
;
; The test harness (Python + Unicorn) writes the input string somewhere in
; memory, sets SHELL_ARGS_PTR to point to it, then calls entry (0x1000).
; After hlt, the test reads ARGV_ARGC and ARGV_PTRS to verify results.

[bits 16]
[org 0x1000]

%include "memory.inc"

entry:
    call shell_parse_args
    hlt

%include "shell_parse_args.inc"
