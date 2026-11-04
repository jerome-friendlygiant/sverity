// sverity-args: --top top --sdc @DIR@/bad_case_unknown_port.sdc
// expect: CST-018 WARNING
// expect: CST-009 WARNING
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, d, output logic q);
  always_ff @(negedge clk) q <= d;
endmodule
