// sverity-args: --top top
// Loop induction variable referenced outside its for loop keeps CST-010 static-local fallback.
// expect: CST-010 WARNING
module top(input logic clk, input logic [3:0] g, output logic [3:0] b);
  function [3:0] gray2bin_leak;
    input [3:0] x;
    integer i;
    begin
      for (i = 0; i < 4; i = i + 1) begin
        gray2bin_leak[i] = ^(x >> i);
      end
      gray2bin_leak[0] = i[0];
    end
  endfunction
  always_ff @(posedge clk) b <= gray2bin_leak(g);
endmodule
