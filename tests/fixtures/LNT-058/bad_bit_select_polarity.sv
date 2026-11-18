// sverity-args: --top top
// The reset is bit 1 of cr and the first if tests that bit high while the edge is negedge: polarity mismatch.
// expect: LNT-058 ERROR 5
module top(input logic [1:0] cr, input logic d, output logic q);
  always_ff @(posedge cr[0] or negedge cr[1])
    if (cr[1]) q <= 1'b0;
    else q <= d;
endmodule
