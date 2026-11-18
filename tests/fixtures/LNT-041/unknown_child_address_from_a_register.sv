// sverity-args: --top top
// The child's address port is fed by a register that only counts 0..3: words 4..7 are never written, but a register's type range does not say which values it takes.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [2:0] cnt;
  always_ff @(posedge clk) cnt <= (cnt == 3'd3) ? 3'd0 : cnt + 3'd1;
  child u(.clk, .we, .wa(cnt), .wd, .ra, .rd);
endmodule
