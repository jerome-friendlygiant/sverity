// sverity-args: --top top --sdc @DIR@/oob_reg_index.sdc
// expect: CST-002 WARNING
// expect-unresolved: CST-002
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, alt, d, output logic q);
  logic [3:2] a_q;
  always_ff @(posedge clk) a_q <= {d, ~d};
  always_ff @(posedge alt) q <= a_q[3];
endmodule
