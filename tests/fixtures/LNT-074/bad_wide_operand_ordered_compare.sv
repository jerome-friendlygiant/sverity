// sverity-args: --top top
// revert: (w < 5) => (w < 64'd5)
// revert: (v != 1) => (v != 40'd1)
// revert-expect-none: LNT-074
// expect: LNT-074 INFO 9
// expect: LNT-074 INFO 10
// expect-count: LNT-074 INFO 2
module top(input logic [63:0] w, input logic [39:0] v, output logic lt, ne);
  assign lt = (w < 5);           // unsized literal against a 64-bit operand
  assign ne = (v != 1);          // unsized literal against a 40-bit operand
endmodule
