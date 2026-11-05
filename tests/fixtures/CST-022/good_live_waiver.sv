// sverity-args: --top top
// Near miss of CST-006/source_stale and source_expired: this waiver matches a finding and has no expiry.
// expect-none: CST-022
module top(input logic a, output logic y);
// sverity-waive LNT-076 owner="team" reason="legacy process"
always @(a) y = a;
endmodule
