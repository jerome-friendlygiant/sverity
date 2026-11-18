// sverity-args: --top top
// Whether u.w is a net or a variable is unknown, so slang cannot check the procedural assignment: the run must not look clean.
// expect: LNT-002 ERROR
// expect: LNT-001 FATAL
// expect-coverage: elaboration INCOMPLETE
module top(input logic a, output logic y);
  missing_mod u();
  always_comb u.w = a;
  assign y = a;
endmodule
