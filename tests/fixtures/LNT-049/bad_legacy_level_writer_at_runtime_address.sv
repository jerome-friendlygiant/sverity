// sverity-args: --top top
// A level-sensitive block writes the memory at a run-time address: latches, written through every address the decoder passes while the
// enable is high. WARNING at the write, not INFO at the declaration.
// expect: LNT-049 WARNING 7
module top(input logic d, i, output logic q);
  logic mem[2];
  always @(d or i) mem[i] = d;
  assign q = mem[i];
endmodule
