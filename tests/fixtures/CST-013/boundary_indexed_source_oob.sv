// sverity-args: --top top --sdc @DIR@/indexed_source_oob.sdc
// expect: CST-002 WARNING
// expect-unresolved: CST-002
// expect-none: CST-013
// expect-coverage: constraints INCOMPLETE
module top(input logic [3:2] clk, output logic q);
  wire out_clk = clk[3];
  always_ff @(posedge out_clk) q <= ~q;
endmodule
