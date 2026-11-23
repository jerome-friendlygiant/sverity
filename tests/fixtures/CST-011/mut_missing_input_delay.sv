// sverity-args: --top top --sdc @DIR@/core_only.sdc
// Mutation: unconstrained captured data input triggers CST-011; switching to core_input_delay.sdc clears it.
// expect: CST-011 WARNING
// revert: core_only.sdc => core_input_delay.sdc
// revert-expect-none: CST-011 WARNING
module top(input logic clk, din, output logic q);
  always_ff @(posedge clk) q <= din;
endmodule
