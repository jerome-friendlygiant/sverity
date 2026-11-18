// sverity-args: --top top
// Both processes list the shared reset first and a different clock second: the read is still on another clock than the write, whichever
// event comes first.
// expect-unresolved: LNT-082 8
module top(input logic clk_w, clk_r, rst, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge rst or posedge clk_w) if (rst) mem <= '{default: '0}; else if (we) mem[wa] <= wd;
  always_ff @(posedge rst or posedge clk_r) if (rst) rd <= '0; else rd <= mem[ra];
endmodule
