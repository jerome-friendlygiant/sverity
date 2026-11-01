// sverity-args: --top top
// Two edge events and neither is tested as a reset: the clock cannot be identified -> WARNING + INCOMPLETE.
// expect: CDC-001 WARNING
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  always_ff @(posedge clka or posedge clkb) q <= d;
endmodule
