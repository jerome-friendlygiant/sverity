// sverity-args: --top top
// The buffer's output feeds its own data input: data reaches the output through the gate, so the loop is certain.
// expect: LNT-012 ERROR
module top(input logic e, output wire y);
  wire a;
  bufif1 (y, a, e);
  assign a = y;
endmodule
