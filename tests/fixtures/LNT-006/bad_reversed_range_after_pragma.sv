// sverity-args: --top top
// The pragma lowers slang's default error for a reversed part-select; the select is still wrong.
// expect: LNT-006 ERROR 7
// expect: LNT-001 FATAL
`pragma diagnostic warn="-Wrange-select-reversed"
module top(input logic [3:0] a, output logic [3:0] y);
  assign y = a[0:3];
endmodule
