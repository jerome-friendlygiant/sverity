// sverity-args: --top top --sdc @DIR@/indexed_source_mismatch.sdc
// expect: CST-013 ERROR
// expect-confidence: CST-013 established
// expect-none: CST-002
// expect-coverage: constraints INCOMPLETE
module top(input logic [3:2] clk, output logic q);
  wire out_clk = clk[3];
  always_ff @(posedge out_clk) q <= ~q;
endmodule
