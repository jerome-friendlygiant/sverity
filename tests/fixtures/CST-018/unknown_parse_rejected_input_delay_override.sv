// sverity-args: --top top --goal cdc --sdc @DIR@/parse_rejected_override.sdc
// A parse-rejected set_input_delay after a synchronous contract must mark the input unresolved.
// expect-unresolved: CST-018
// expect-unresolved: CST-004
// expect-unresolved: CDC-029
// expect-none: CDC-029 INFO
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, e, output logic q);
  always_ff @(posedge clk) q <= e;
endmodule
