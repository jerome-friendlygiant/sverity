// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-043 WARNING
// expect-unresolved: CDC-003
// expect-none: CDC-003 INFO
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic launch, s1;
  always_ff @(posedge clka) launch <= d;
  always @(posedge clkb or negedge clkb) s1 <= launch;
  always @(posedge clkb or negedge clkb) q <= s1;
endmodule
