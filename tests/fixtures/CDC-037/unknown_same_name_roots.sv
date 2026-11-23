// sverity-args: --top top --sdc @DIR@/unknown.sdc
// The two distinct roots are assigned the same SDC name, causing the relationship to be treated as synchronous.
// expect: CDC-037 WARNING
// expect-unresolved: CDC-037
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic launched;
  always_ff @(posedge clka) launched <= d;
  always_ff @(posedge clkb) q <= launched;
endmodule
