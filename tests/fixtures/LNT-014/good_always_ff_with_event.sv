// sverity-args: --top top
// Near-miss of bad_always_ff_no_event: always_ff has exactly one edge event control.
// expect-none: LNT-014
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
