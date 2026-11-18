// sverity-args: --top top
// expect-none: LNT-058
// Bit 0 is the clock and bit 1 the reset: distinct controls, and the reset is tested with the matching polarity.
module top(input logic [1:0] cr, input logic d, output logic q);
  always_ff @(posedge cr[0] or negedge cr[1])
    if (!cr[1]) q <= 1'b0;
    else q <= d;
endmodule
