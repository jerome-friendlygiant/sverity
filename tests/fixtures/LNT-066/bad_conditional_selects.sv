// sverity-args: --top top
// Each condition site reports once even when reset and constant drivers seed it.
// expect-count: LNT-066 WARNING 3
// expect: LNT-066 WARNING 10
// expect: LNT-066 WARNING 11
// expect: LNT-066 WARNING 12
module top(input logic clk, rst_n, a, b, output logic y, z, w);
 logic st;
 always_ff @(posedge clk or negedge rst_n) if(!rst_n) st<=1'bx; else st<=a;
 assign y = st ? a : b;
 always_comb z = st ? a : b;
 always_comb case(st) 1'b0: w=a; default: w=b; endcase
endmodule
