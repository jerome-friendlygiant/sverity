// sverity-args: --top top
// The same shape on one clock with the reset listed first on both processes: read-first behaviour is defined.
// expect-none: LNT-082
module top(input logic clk, rst, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge rst or posedge clk) if (rst) mem <= '{default: '0}; else if (we) mem[wa] <= wd;
  always_ff @(posedge rst or posedge clk) if (rst) rd <= '0; else rd <= mem[ra];
endmodule
