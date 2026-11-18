// sverity-args: --top top
// `a & 4'b0` is constant zero whatever `a` holds: the read does not reach the output, so the loop is not proven.
// expect-none: LNT-012 ERROR
module top(input logic [3:0] x, output logic [3:0] a);
  always_comb a = a & 4'b0;
endmodule
