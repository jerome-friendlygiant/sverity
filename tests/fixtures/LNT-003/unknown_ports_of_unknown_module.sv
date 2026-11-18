// sverity-args: --top top
// The instantiated module is not in the design, so slang cannot check its connections: the run must not look clean.
// expect: LNT-002 ERROR
// expect: LNT-001 FATAL
// expect-coverage: elaboration INCOMPLETE
module top(input logic a, output logic y);
  missing_mod u(.a(a), .nosuch(y));
endmodule
