// sverity-args: --top top
// A waiver that matches nothing waives nothing and changes no hardware: advisory.
// expect: CST-022 INFO
module top(input logic f, output logic y);
// sverity-waive CDC-003 owner="team" reason="wrong target"
always @(f) y = f;
endmodule
