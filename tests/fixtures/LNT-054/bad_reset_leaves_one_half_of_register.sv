// sverity-args: --top top
// Two processes load the halves of one register but only the low half is reset.
// expect: LNT-054 WARNING 9
module top(input logic clk, rst, input logic [7:0] d, output logic y);
  logic [7:0] q;
  always_ff @(posedge clk) if (rst) q[3:0] <= '0; else q[3:0] <= d[3:0];
  always_ff @(posedge clk) q[7:4] <= d[7:4];
  always_comb begin
    if (q[7]) y = 1'b1; else y = 1'b0;
  end
endmodule
