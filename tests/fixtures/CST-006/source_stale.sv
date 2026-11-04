// sverity-args: --top top
// expect: CST-022 WARNING
module top(input logic f, output logic y);
// sverity-waive CDC-003 owner="team" reason="wrong target"
always @(f) y = f;
endmodule
