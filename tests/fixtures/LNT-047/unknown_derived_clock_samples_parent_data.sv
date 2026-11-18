// sverity-args: --top top
// slow_clk is the output of a register on clk, and cap samples d, a register on clk, on slow_clk: which value it reads depends on the clock-tree skew.
// expect: LNT-047 INFO 12
// expect-unresolved: LNT-047 11
module top(input logic clk, rst_n, output logic [7:0] cap);
  logic slow_clk;
  logic [7:0] d;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin slow_clk <= 1'b0; d <= '0; end
    else begin slow_clk <= ~slow_clk; d <= d + 1'b1; end
  always_ff
    @(posedge slow_clk or negedge rst_n)
    if (!rst_n) cap <= '0; else cap <= d;
endmodule
