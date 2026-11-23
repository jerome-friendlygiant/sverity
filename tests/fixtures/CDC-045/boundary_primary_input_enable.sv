// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary/unknown: the unlatched gate is a CDC-045 heuristic; its primary-input enable is separately unresolved CDC-020.
// expect: CDC-045 WARNING
// expect-confidence: CDC-045 heuristic
// expect: CDC-020 WARNING
// expect-unresolved: CDC-020
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, en_in, d, output logic q);
  logic gclk;
  assign gclk = clka & en_in;
  always_ff @(posedge gclk) q <= d;
endmodule
