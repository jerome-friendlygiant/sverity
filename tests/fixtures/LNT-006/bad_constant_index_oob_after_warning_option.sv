// sverity-args: --top top -Windex-oob
// -Windex-oob lowers slang's default error to a warning; a constant index past the range is still proven wrong.
// expect: LNT-006 ERROR 7
// An out-of-range select is legal (the LRM defines X), so it is no elaboration failure: the run goes on.
// expect-none: LNT-001
module top(input logic [3:0] a, output logic y);
  assign y = a[5];
endmodule
