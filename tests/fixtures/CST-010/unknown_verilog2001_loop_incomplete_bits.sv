// sverity-args: --top top
// For loop fills only bits [2:0] and leaves return bit [3] unassigned: return value is incomplete.
// expect: CST-010 WARNING
module top(input logic clk, input logic [3:0] g, output logic [3:0] b);
  function [3:0] gray2bin_partial;
    input [3:0] x;
    integer i;
    begin
      for (i = 0; i < 3; i = i + 1) begin
        gray2bin_partial[i] = ^(x >> i);
      end
    end
  endfunction
  always_ff @(posedge clk) b <= gray2bin_partial(g);
endmodule
