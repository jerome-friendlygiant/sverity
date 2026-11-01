// sverity-args: --top top
// expect: LNT-034 INFO 5
// expect-count: LNT-034 INFO 1
module top(input logic c, input logic [7:0] a, input logic [63:0] w, output logic [63:0] o, output logic [7:0] p, output logic [7:0] q);
  assign o = c ? w : 0;          // unsized decimal against a 64-bit arm
  assign p = c ? a : 'h0;        // unsized based literal: no finding
  assign q = c ? a : 8'h0;       // sized literal of the same width: no finding
endmodule
