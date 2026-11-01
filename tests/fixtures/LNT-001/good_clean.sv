// sverity-args: --top top
// expect-none: LNT-001
// expect-coverage: elaboration COMPLETE
module top(input logic a, output logic y);
  assign y = ~a;
endmodule
