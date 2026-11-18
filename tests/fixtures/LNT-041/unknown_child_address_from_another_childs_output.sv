// sverity-args: --top top
// The address is the output of another instance, which only ever produces 0 or 1.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module enc(input logic i, output logic [2:0] o);
  assign o = {2'b0, i};
endmodule
module top(input logic clk, we, raw, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [2:0] w;
  enc e(.i(raw), .o(w));
  child u(.clk, .we, .wa(w), .wd, .ra, .rd);
endmodule
