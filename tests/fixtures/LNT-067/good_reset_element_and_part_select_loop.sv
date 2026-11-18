// sverity-args: --top top
// Element 0 is reset whole; the other branch loads it by part-selects in a loop that covers the same bits.
// expect-none: LNT-067
// expect-none: LNT-020
module top(input logic clk, rst_ni, input logic [7:0] wd, output logic [7:0] y);
  logic [31:0] key_reg [2];
  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) key_reg[0] <= '0;
    else for (int j = 0; j < 4; j++) key_reg[0][8*j +: 8] <= wd;
  end
  assign y = key_reg[0][7:0];
endmodule
