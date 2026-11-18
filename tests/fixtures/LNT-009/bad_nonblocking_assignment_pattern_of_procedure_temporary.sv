// sverity-args: --top top
// `t` is written before it is read in the combinational block, but the nonblocking assignment-pattern write builds a flop that
// also drives it: state, two drivers.
// expect: LNT-009 ERROR 9
module top(input clk, input [1:0] a, b, output logic [1:0] y);
  typedef struct packed {logic [1:0] lo, hi;} pair_t;
  logic [1:0] t, u;
  always @* begin t = a; y = t; end
  always @(posedge clk) begin t = b; u = 0; pair_t'{t, u} <= pair_t'('0); end
endmodule
