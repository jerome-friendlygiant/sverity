// sverity-args: --top top
// expect-none: LNT-011
module top(input logic a, output logic y);
  assign y = ~a;
endmodule
