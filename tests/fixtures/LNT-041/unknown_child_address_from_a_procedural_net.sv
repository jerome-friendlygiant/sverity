// sverity-args: --top top
// The address is a procedural net that is 0 or 6: which words are written is not the type's range.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, raw, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [2:0] w;
  always_comb begin
    w = 3'd0;
    if (raw) w = 3'd6;
  end
  child u(.clk, .we, .wa(w), .wd, .ra, .rd);
endmodule
