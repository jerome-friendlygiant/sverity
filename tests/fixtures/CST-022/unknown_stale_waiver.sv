// sverity-args: --top top
// Unknown: stale waiver matching no finding in the run is disclosed under CST-022 INFO.
// expect: CST-022 INFO
module top(input logic a, output logic y);
// sverity-waive LNT-076 owner="team" reason="stale waiver"
assign y = a;
endmodule
