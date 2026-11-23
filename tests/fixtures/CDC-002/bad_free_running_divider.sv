// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
module top(input logic clka, clkb, d, output logic q);
  logic divided;
  always_ff @(posedge clka) divided <= ~divided;
  always_ff @(posedge divided) q <= d;
endmodule
