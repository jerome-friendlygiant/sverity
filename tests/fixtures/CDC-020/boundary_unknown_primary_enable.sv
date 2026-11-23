// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unknown: a primary-input gate enable has no launch relation, so the result is Unresolved.
// expect: CDC-020 WARNING
// expect-unresolved: CDC-020
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, en_in, d, output logic q);
  logic gclk;
  assign gclk = clka & en_in;
  always_ff @(posedge gclk) q <= d;
endmodule
