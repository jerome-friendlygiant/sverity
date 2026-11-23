// sverity-args: --top top --goal cdc --sdc @DIR@/mixed_async_unknown.sdc
// Mixed Async + Unknown input-delay modes must emit CST-004 and keep synchronized CDC-029 unresolved.
// expect-unresolved: CST-004
// expect-unresolved: CDC-029
// expect-none: CDC-029 INFO
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, e, output logic q);
  logic s1, s2;
  always_ff @(posedge clk) {s2, s1} <= {s1, e};
  assign q = s2;
endmodule
