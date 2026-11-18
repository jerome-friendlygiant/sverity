// sverity-args: --top top
// An X written inside a final block is simulation-only: INFO, not WARNING.
// expect: LNT-039 INFO 8
// expect-none: LNT-039 WARNING
module top(input logic clk, input logic d, output logic q);
  logic dbg;
  always_ff @(posedge clk) q <= d;
  final dbg = 1'bx;
endmodule
