// sverity-args: --top top --sdc @DIR@/div2.sdc
// Declared divide_by 2 disagrees with direct combinational alias (divide_by 1).
// expect: CST-013 ERROR
// expect-coverage: constraints INCOMPLETE
module top(input logic clk_src, d, output logic q);
  logic div2_q;
  assign div2_q = clk_src;
  always_ff @(posedge div2_q) q <= d;
endmodule
