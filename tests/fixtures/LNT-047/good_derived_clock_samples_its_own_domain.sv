// sverity-args: --top top
// cap is on the derived clock and reads only a register on that clock: nothing crosses from the parent clock.
// expect: LNT-047 INFO 9
// expect-none: LNT-047 WARNING
module top(input logic clk, rst_n, output logic [7:0] cap);
  logic slow_clk;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) slow_clk <= 1'b0; else slow_clk <= ~slow_clk;
  always_ff @(posedge slow_clk or negedge rst_n)
    if (!rst_n) cap <= '0; else cap <= cap + 1'b1;
endmodule
