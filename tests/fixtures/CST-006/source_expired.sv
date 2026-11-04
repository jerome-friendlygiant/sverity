// sverity-args: --top top
// expect: CST-022 WARNING
module top(input logic e, output logic y);
// sverity-waive LNT-076 owner="team" reason="old exception" expires="2020-01-01"
always @(e) y = e ^ e;
endmodule
