// sverity-args: --top top
// Boundary + mutation: placeholder waiver owner triggers CST-006 WARNING; replacing with a real owner clears it.
// expect: CST-006 WARNING
// revert: owner="TODO" => owner="rtl-team"
// revert-expect-none: CST-006 WARNING
module top(input logic a, output logic y);
// sverity-waive LNT-076 owner="TODO" reason="legacy process"
always @(a) y = a;
endmodule
