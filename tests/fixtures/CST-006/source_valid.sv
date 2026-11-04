// sverity-args: --top top
// expect-none: CST-006
// expect-none: CST-022
module top(input logic a, output logic y);
// sverity-waive LNT-076 owner="team" reason="legacy process"
always @(a) y = a;
endmodule
