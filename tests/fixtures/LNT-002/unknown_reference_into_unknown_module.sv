// sverity-args: --top top
// u.x names a signal inside a module that is not in the design: only the module is reported, the run must not look clean.
// expect: LNT-002 ERROR 7
// expect: LNT-001 FATAL
// expect-coverage: elaboration INCOMPLETE
module top(output logic y);
  missing_mod u();
  assign y = u.x;
endmodule
