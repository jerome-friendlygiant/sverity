// sverity-args: --top top --sdc @DIR@/bad_case_unknown_value.sdc
// expect: CST-018 WARNING
// expect: CDC-003 ERROR
// expect-coverage: constraints INCOMPLETE
module top(input logic clk_a, clk_b, mode, output logic q);
  logic data_q;
  always_ff @(posedge clk_a) data_q <= ~data_q;
  always_ff @(posedge clk_b) q <= mode ? data_q : 1'b0;
endmodule
