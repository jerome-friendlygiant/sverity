// sverity-args: --top top
// The selector's type is unknown, so slang cannot check case coverage: the run must not look clean.
// expect: LNT-002 ERROR
// expect: LNT-001 FATAL
// expect-coverage: elaboration INCOMPLETE
module top(input logic a, output logic y);
  missing_mod u();
  always_comb case (u.sel) 2'd0: y = a; 2'd1: y = ~a; endcase
endmodule
