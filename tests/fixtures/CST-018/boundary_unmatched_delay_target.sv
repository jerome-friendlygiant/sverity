// sverity-args: --top top --sdc @DIR@/invalid_zero_port_delay_target.sdc
// expect: CST-018 WARNING
// expect-unresolved: CST-018
// expect-unresolved: CDC-029
// expect-coverage: constraints INCOMPLETE
// revert: invalid_zero_port_delay_target.sdc => valid_zero_port_delay.sdc
// revert-expect-none: CST-018
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
