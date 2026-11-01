// sverity-args: --top top --option LNT-036.report_modular=true
// expect: LNT-036 INFO 5
// expect-count: LNT-036 INFO 1
module top(input logic [7:0] a, b, output logic [7:0] s, output logic [15:0] p);
  assign s = a + b;                 // operands and target are 8 bits: the carry is discarded
  assign p = a * b;                 // no finding
endmodule
