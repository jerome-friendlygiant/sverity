// sverity-args: --top top
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic d, output logic q);
  logic no_driver;
  always_ff @(posedge no_driver) q <= d;
endmodule
