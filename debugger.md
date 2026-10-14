You catch these invisible bugs by cross-referencing three different layers of "truth": the wire, the memory, and the kernel. When your C code lies to you, one of these tools will immediately expose the contradiction.

1. The Wire Truth: `tcpdump`
- If you had run a packet sniffer on tap0, the "Double-Bounce Reflection" would have been exposed instantly.
- Normally, a ping looks like two lines:
    - Host -> Tap: ICMP Echo Request
    - Tap -> Host: ICMP Echo Reply
- If you ran sudo tcpdump -i tap0 -n -e -vvv -X, you would have seen four lines:
    - Host -> Tap: ICMP Echo Request
    - Tap -> Host: ICMP Echo Request (Wait, why is my C program sending a request?)
    - Host -> Tap: ICMP Echo Reply (Ah, the kernel is replying to my program's request!)
    - Tap -> Host: ICMP Echo Reply

How to use it: Always keep a tcpdump pane open watching your virtual interface while you test.

2. The Memory Truth: GDB & AddressSanitizer (ASan)
- C will happily let you read memory you shouldn't. To catch the pointer arithmetic bug (`rx_ip + 20`), you force the compiler to watch your memory boundaries.
- AddressSanitizer (ASan): Compile your code with gcc -fsanitize=address -g main.c. If your pointer arithmetic jumps outside the bounds of the packet data into uninitialized memory, ASan will immediately crash the program and print a stack trace showing exactly which line of C code caused the illegal memory read.
- GDB (GNU Debugger): You could step through the code and print the physical distance between your pointers:
```
(gdb) p (void*)icmp - (void*)buffer
$1 = 414
```
Seeing 414 instead of 34 instantly tells you the pointer overshot the header.

3. The Syscall Truth: `strace`
- Sometimes you don't know if the bug is in your packet parsing or if the Linux kernel is dropping the packet silently before it even reaches your program.
- Running your program through strace (sudo strace -e read,write -x ./my_stack) intercepts the exact hex bytes crossing the boundary between the kernel and your tap_fd. It proves whether the kernel actually handed you the packet, or if your program successfully pushed a buffer to write().
