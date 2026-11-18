// sverity-args: --top top
// The loop (counter declared outside the header) fills r[3:0] only: r[7:4] is read before it is written and holds state.
// expect: LNT-013 WARNING 10
module top(input [7:0] a, output reg [7:0] y);
  function [7:0] f(input [7:0] v);
    integer j;
    reg [7:0] r;
    begin
      for (j = 0; j < 4; j = j + 1) r[j] = v[j];
      f = r;
    end
  endfunction
  assign y = f(a);
endmodule
