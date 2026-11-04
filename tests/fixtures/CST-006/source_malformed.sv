// sverity-args: --top top
// expect: CST-006 WARNING
module top(input logic d, output logic y);
// sverity-waive LNT-076 owner=team reason="unquoted"
always @(d) y = d | d;
endmodule
