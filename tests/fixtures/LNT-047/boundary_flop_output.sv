// sverity-args: --top top
// A clock that a register drives is a derived clock: built as written, disclosed as INFO naming the register, never the WARNING.
// expect: LNT-047 INFO 8
// expect-none: LNT-047 WARNING
module top(input logic clk, a, output logic q);
  logic s;
  always_ff @(posedge clk) s <= a;
  always_ff @(posedge s) q <= a;
endmodule
