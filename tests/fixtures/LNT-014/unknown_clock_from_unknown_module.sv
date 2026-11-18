// sverity-args: --top top
// The clock is a signal of an unknown module, so slang does not check the procedure: the run must not look clean.
// expect: LNT-002 ERROR
// expect: LNT-001 FATAL
// expect-coverage: elaboration INCOMPLETE
module top(input logic d, output logic q);
  missing_mod u();
  always_ff @(posedge u.clk) q = d;
endmodule
