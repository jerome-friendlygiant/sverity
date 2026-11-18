// sverity-args: --top top
// One clocked process writes with nonblocking assignments, but the read is registered on another clock: a same-address collision is
// asynchronous, and its result depends on the memory, which simulation does not show.
// expect-unresolved: LNT-082 8
module top(input logic clk_w, clk_r, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge clk_w) if (we) mem[wa] <= wd;
  always_ff @(posedge clk_r) rd <= mem[ra];
endmodule
