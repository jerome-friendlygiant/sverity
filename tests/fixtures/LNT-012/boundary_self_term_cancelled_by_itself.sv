// sverity-args: --top top
// `a ^ a` is zero whatever `a` holds: the read does not reach the output, so the loop is not proven.
// expect-none: LNT-012 ERROR
module top(input logic [3:0] x, output logic [3:0] a);
  always_comb a = a ^ a ^ x;
endmodule
