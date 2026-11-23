// sverity-args: --top top
// Boundary: waiver with a future expiry date is still valid and emits no CST-022 warning.
// expect-none: CST-022
module top(input logic a, output logic y);
// sverity-waive LNT-076 owner="team" reason="legacy process" expires="2099-12-31"
always @(a) y = a;
endmodule
