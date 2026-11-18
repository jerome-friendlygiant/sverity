// sverity-args: --top top
// A zero-trip loop is possible when m is 0: y keeps its old value. The loop is not unrolled, and its body is not dead.
// expect: LNT-013 WARNING 5
module top(input [7:0] a, input [3:0] m, output reg [7:0] y);
  always @* begin
    for (int j = 0; j < m; j++) y[j] = a[j];
  end
endmodule
