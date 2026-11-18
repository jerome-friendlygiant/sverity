// sverity-args: --top top
// Both arms of the mux are 2-bit slices of free inputs, zero-extended into the 3-bit address: it reaches words 0 to 3 only, so words 4 to 7 are never written.
// expect: LNT-041 WARNING 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, s, input logic [2:0] p, q, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa(s ? p[1:0] : q[1:0]), .wd, .ra, .rd);
endmodule
