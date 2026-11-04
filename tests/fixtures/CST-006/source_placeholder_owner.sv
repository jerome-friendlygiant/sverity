// sverity-args: --top top
// expect: CST-006 WARNING
module top(input logic b, output logic y);
// sverity-waive LNT-076 owner="TBD" reason="legacy process"
always @(b) y = ~b;
endmodule
