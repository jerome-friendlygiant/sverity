// sverity-args: --top top
// 3'(s) of a free signed input: no loop bounds its value, so a negative s changes the unsigned result.
// expect: LNT-005 WARNING 5
module top(input logic signed [7:0] s, output logic [2:0] o);
  always_comb o = 3'(s);
endmodule
