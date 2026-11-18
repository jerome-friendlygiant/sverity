// sverity-args: --top top
// `t` is written before it is read in the combinational block, but the nonblocking concatenation write `{t[0], u[0]}` builds a flop
// that also drives it: state, two drivers.
// expect: LNT-009 ERROR 8
module top(input clk, input [1:0] a, b, output logic [1:0] y);
  logic [1:0] t, u;
  always @* begin t = a; y = t; end
  always @(posedge clk) {t[0], u[0]} <= {b[0], b[1]};
endmodule
