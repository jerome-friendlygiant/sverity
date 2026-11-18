// sverity-args: --top top
// A pass switch is bidirectional and the model does not follow which side drives which: a loop that needs its edges is Unresolved.
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic x, output wire y);
  wire a, b;
  tran (a, b);
  assign b = ~a & x;
  assign y = b;
endmodule
