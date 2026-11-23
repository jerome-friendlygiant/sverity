// sverity-args: --top top --sdc @FIXTURES@/CST-018/rejected_synchronous_override.sdc
// A rejected set_input_delay override after a synchronous contract must not suppress CDC-044.
// expect: CDC-044 WARNING
module top(input logic clk, e, output logic pulse);
  logic s1, s2, s3;
  always_ff @(posedge clk) {s3, s2, s1} <= {s2, s1, e};
  assign pulse = s2 ^ s3;
endmodule
