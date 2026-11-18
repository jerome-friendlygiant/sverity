// sverity-args: --top top
// A size cast of a small constant is a signed value that is not negative: the ordered test sees the same value either way.
// expect: LNT-005 INFO 6
// expect-none: LNT-005 WARNING
module top #(parameter int unsigned W = 12) (input logic [3:0] n, output logic y);
  assign y = (n >= 4'(2));
endmodule
