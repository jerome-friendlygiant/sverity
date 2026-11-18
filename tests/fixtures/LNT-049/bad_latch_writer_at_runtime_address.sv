// sverity-args: --top top
// An always_latch memory write: while `we` is high, every address `wa` passes through is written.
// expect: LNT-049 WARNING 6
module top(input logic we, input logic [2:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] m [8];
  always_latch if (we) m[wa] = wd;
  assign rd = m[ra];
endmodule
