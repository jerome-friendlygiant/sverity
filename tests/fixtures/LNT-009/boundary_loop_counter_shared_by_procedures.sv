// sverity-args: --top top
// Near-miss of bad_two_always_procedures: a function's own loop variable and a module-level for-loop counter
// shared by two always blocks are unrolled by synthesis; neither is a second driver.
// expect-none: LNT-009
module top(input clk, input [3:0] g, output reg [3:0] a, b, c, d);
function [3:0] gray2bin(input [3:0] g);
    integer i;
    for (i = 0; i <= 3; i = i + 1) gray2bin[i] = ^(g >> i);
endfunction
integer k;
always @(posedge clk) a <= gray2bin(g);
always @(posedge clk) b <= gray2bin(~g);
always @(posedge clk) for (k = 0; k < 4; k = k + 1) c[k] <= g[k];
always @(posedge clk) for (k = 0; k < 4; k = k + 1) d[k] <= ~g[k];
endmodule
