// sverity-args: --top top
// Bad + mutation: expired waiver triggers CST-022 WARNING; extending expiry clears it.
// expect: CST-022 WARNING
// revert: expires="2000-01-01" => expires="2099-12-31"
// revert-expect-none: CST-022 WARNING
module top(input logic a, output logic y);
// sverity-waive LNT-076 owner="team" reason="legacy process" expires="2000-01-01"
always @(a) y = a;
endmodule
