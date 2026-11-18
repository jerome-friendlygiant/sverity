// sverity-args: --top top
// The reset loop skips word 0 under an `if`, so word 0 is not reset.
// expect: LNT-054 WARNING 8
module top(input logic clk, rst, input logic [1:0] a, input logic [7:0] d, output logic y);
  logic [7:0] rf [4];
  always_ff @(posedge clk) if (rst) begin for (int i = 0; i < 4; i++) if (i != 0) rf[i] <= '0; end else rf[a] <= d;
  always_comb begin
    if (rf[0][0]) y = d[0]; else y = 1'b0;
  end
endmodule
