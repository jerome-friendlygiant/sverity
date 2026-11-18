// sverity-args: --top top
// Verilog-2001 style: the counter is a function-local integer, not declared by the for header. The loop has constant bounds, so
// every bit of the result and of the local is written before it is read.
// expect-none: LNT-013
module top(input [7:0] a, output reg [7:0] y1, y2);
  function [7:0] rev(input [7:0] v);
    integer j;
    begin
      for (j = 0; j < 8; j = j + 1) rev[j] = v[7-j];
    end
  endfunction
  function [7:0] rev2(input [7:0] v);
    integer j;
    reg [7:0] r;
    begin
      for (j = 7; j >= 0; j = j - 1) r[j] = v[7-j];
      rev2 = r;
    end
  endfunction
  assign y1 = rev(a);
  assign y2 = rev2(a);
endmodule
