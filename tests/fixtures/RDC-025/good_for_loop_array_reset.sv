// sverity-args: --top top
// A constant-bound for loop resetting every element of an unpacked array gives every bit a decided constant reset value with no RDC-019 false alarm.
// expect-none: RDC-010
// expect-none: RDC-019
// expect-none: RDC-020
// expect-none: RDC-025
module top(input logic clk, rst_n, we, input logic [1:0] wa, input logic [3:0] wd, output logic [3:0] y);
  logic [3:0] bank [4];
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      for (int i = 0; i < 4; i++) bank[i] <= '0;
    end else if (we) begin
      bank[wa] <= wd;
    end
  assign y = bank[0];
endmodule
