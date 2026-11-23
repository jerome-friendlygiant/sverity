// sverity-args: --top top --sdc @DIR@/offset_reg_empty_inst.sdc
// expect-none: CST-002
module empty(); endmodule
module top(input logic clk, alt, d, output logic q);
  logic [3:2] a_q;
  empty v();
  always_ff @(posedge clk) a_q <= {d, ~d};
  always_ff @(posedge alt) q <= a_q[3];
endmodule
