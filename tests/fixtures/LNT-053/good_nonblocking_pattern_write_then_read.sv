// sverity-args: --top top
// The assignment-pattern write is nonblocking: the read after it still sees `t = b`, so that write is not overwritten.
// expect-none: LNT-053
module top(input clk, input [1:0] b, output logic [1:0] y);
  typedef struct packed {logic [1:0] lo, hi;} pair_t;
  logic [1:0] t, u;
  always @(posedge clk) begin
    t = b; u = 0;
    pair_t'{t, u} <= pair_t'('0);
    y <= t;
  end
endmodule
