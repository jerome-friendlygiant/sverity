// sverity-args: --top top
// expect: LNT-001 ERROR
// expect: LNT-001 FATAL
// expect-count: LNT-001 ERROR 1
// expect-coverage: elaboration INCOMPLETE
module top(input logic a, output logic y)
  assign y = a;
endmodule
