// sverity-args: --top top
// The reset is listed twice on the same edge through an exact copy (`wire r = rst_n`): one control with its known forced value
// (0). It is not a set/clear conflict and the output is not left with an undecided reset.
// expect-none: RDC-026
// expect-none: RDC-025
module top(input logic clka, rst_n, d, output logic q);
  wire r = rst_n;
  always_ff @(posedge clka or negedge rst_n or negedge r)
    if (!rst_n || !r) q <= 0; else q <= d;
endmodule
