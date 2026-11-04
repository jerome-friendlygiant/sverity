// sverity-args: --top top
// expect: CST-006 WARNING
module top(input logic c, output logic y);
// sverity-waive LNT-999 owner="team" reason="typo"
always @(c) y = c & c;
endmodule
