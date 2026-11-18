// sverity-args: --top top
// a + b + 2 is judged on the whole chain: the constant addend is modular by intent, the sum under it still loses its carry.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, output logic gt);
  assign gt = (a + b + 8'd2) > 8'd20;
endmodule
