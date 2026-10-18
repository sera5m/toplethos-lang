# native_seq — trapdoored on-device array action

`native("wave", …)` is one interpreter op **plus** a string lookup, **plus**
(on tyrant) a sprintf of Toplethos source sent over UART. A burst of twelve
calls is twelve translates.

`native_seq` packs those calls into an array of interned nids and walks it
in **C**, like `do n[N]` uses `OP_TRAP_LOOP`.

```toplethos
print(native_seq(
  native("wave", 1, 1000, 50, 4, 80),
  native("delay", 200),
  native("adc", 1),
  native("wave_stop")
));
```

Compile-time args must be constants (the trapdoor cannot wait on the
interpreter). Runtime values go in a packed `i32` array, stride 7:

```
[nid, nargs, a0, a1, a2, a3, a4] × N
```

See OIRIA `os_code/core/rs_vm/NSEQ.md` for nids and the `NSQ1` UART blob
(`RSDOM_TYPE_NSEQ = 0x18`). Opcode `RSVM_OP_NATIVE_SEQ = 0xF2`.
