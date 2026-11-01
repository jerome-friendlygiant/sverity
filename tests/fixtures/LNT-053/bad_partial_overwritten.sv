// sverity-args: --top top
// a partial write fully covered by a later write is overwritten before any read (blocking and nonblocking)
// expect: LNT-053 WARNING 7
// expect: LNT-053 WARNING 11
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] y, z);
  always_comb begin
    y[3:0] = a[3:0];
    y = b;
  end
  always_ff @(posedge clk) begin
    z[3:0] <= a[3:0];
    z <= b;
  end
endmodule
