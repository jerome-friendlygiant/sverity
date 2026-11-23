// sverity-args: --top top
// Verilog-2001 function with static loop induction variable filling all return bits via bounded for loop.
// expect-none: CST-010
module top(input logic clk, input logic [3:0] g, output logic [3:0] b);
  function [3:0] gray2bin;
    input [3:0] x;
    integer i;
    begin
      for (i = 0; i < 4; i = i + 1) begin
        gray2bin[i] = ^(x >> i);
      end
    end
  endfunction
  always_ff @(posedge clk) b <= gray2bin(g);
endmodule
