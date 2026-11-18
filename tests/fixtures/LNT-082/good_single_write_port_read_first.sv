// sverity-args: --top top
// One clocked process writes with nonblocking assignments and reads in the same process: the read sees the word as it was before the edge.
// expect-none: LNT-082
module top(input logic clk, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] q);
  logic [7:0] mem [16];
  always_ff @(posedge clk) begin
    if (we) mem[wa] <= wd;
    q <= mem[ra];
  end
endmodule
